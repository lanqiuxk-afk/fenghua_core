# Fenghua Core

把 **PC 的键盘鼠标** 映射成 **Android 触摸** 的接收端（C++17，无界面）。

```
┌──────────────┐    UDP 56789 (协议见 include/protocol.h)    ┌────────────────────────┐
│ PC 发送端     │ ─────────────────────────────────────────► │ Android 端 (本项目)      │
│ fenghua_sender│   [type][len][payload]                    │ 收包 -> 映射 -> 注入触摸  │
│ 钩子 + 停靠窗口│                                           │ ↓                      │
└──────────────┘                                           │ IDriver (内核注入驱动)   │
                                                           └────────────────────────┘
```

配套的 PC 发送端：[**fenghua_sender**](https://github.com/lanqiuxk-afk/fenghua_sender_core)（键鼠捕获 + 投屏）

## 功能

- 按键映射：PC 键绑到屏幕坐标，按下时在半径内随机落点注入
- 虚拟摇杆：WASD + 疾跑/静步，贝塞尔弧线滑动、8 向量化、斜向随机漂移
- 指针三种模式：自由 / 约束（框内滑动，撞边回中）/ 陀螺仪
- 多指槽位管理（5..15 池，按键 / 指针 / 点击互不冲突）
- 配置 JSON 读写；本机 USB 键鼠直连（可选）
- 内核注入通过 `src/driver/driver.h` 的接口接入

## 目录结构

```
fenghua_core/
├── CMakeLists.txt               Android 端构建 (NDK)
├── build_android.bat
├── include/
│   ├── paradise_api.h           驱动头文件 (参考用; 实际对接需你自备)
│   └── protocol.h               PC <-> Android 通信协议 (两端共用)
├── src/
│   ├── main.cpp                 入口: 解析参数、初始化驱动、拉起服务
│   ├── core/engine.{h,cpp}      协议 -> 映射 -> 注入 的全部业务逻辑
│   ├── driver/
│   │   ├── driver.h             IDriver 接口 (接缝都在这)
│   │   ├── stub_driver.cpp      空实现 + 驱动注册表 (也是写新驱动的模板)
│   │   └── paradise_driver.cpp  Paradise 参考实现 (未提供驱动库时自动退化)
│   ├── io/
│   │   ├── injector.{h,cpp}     注入入口: 逻辑坐标 -> 驱动坐标 + 翻转
│   │   ├── udp_server.{h,cpp}   UDP 收包 + ping/pong
│   │   ├── tcp_server.{h,cpp}   TCP 收包 (备用通道)
│   │   ├── direct_input.{h,cpp} 本机 USB 键鼠直连 (可选)
│   │   ├── mapping_manager.*    键位/摇杆/指针配置 + JSON 读写
│   │   └── joystick_controller.* 摇杆方向状态机
│   └── util/
│       ├── keys.{h,cpp}         键码名 / 本机 IP / 时钟
│       └── json.hpp             nlohmann/json (单头)
└── configs/
    └── mappings.example.json    配置样例 (带注释说明)
```

## 编译

### Android 端（arm64-v8a）

需要 NDK r25+（开发时用 r27）和 CMake 3.22：

```cmd
:: 改 build_android.bat 里的 ANDROID_HOME / NDK 路径, 然后:
build_android.bat
```

或手动：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
```

产物：`build/fenghua_core`（约 650 KB，静态链接 libc++，单文件可直接推设备）。

### Windows 发送端

见独立仓库 [fenghua_sender](https://github.com/lanqiuxk-afk/fenghua_sender_core)，
两边共用同一份协议定义（`include/protocol.h`）。

## 运行

```bash
adb push build/fenghua_core /data/local/tmp/
adb shell su -c "chmod 755 /data/local/tmp/fenghua_core"
adb shell su -c "/data/local/tmp/fenghua_core --driver stub --config /data/adb/fenghua/mappings.json"
```

```
============================================================
  Fenghua core (headless)   driver=stub  port=56789
============================================================
  screen (landscape): 2400 x 1080
  config: 16 key mappings, 1 joysticks
  device ip: 192.168.1.23
  put this ip into the PC sender. Ctrl+C to quit.
```

命令行参数：

| 参数 | 说明 |
|---|---|
| `--driver <name>` | 注入驱动，默认 `stub`（可用 `stub`, `paradise`） |
| `--port <n>` | UDP 端口，默认 56789 |
| `--screen <WxH>` | 逻辑分辨率（横屏给出），默认读 `wm size` |
| `--config <path>` | 配置 json 路径 |
| `--logdir <path>` | 日志目录，默认 `/data/adb/fenghua` |
| `--quiet` | 关闭日志 |

PC 侧：`fenghua_sender.exe <设备IP>`，按 **F12** 开始/停止捕获。

## 通信协议

`include/protocol.h` 是两端共用的唯一定义。每个数据报：

```
[1B type][2B payload 长度(小端)][payload]
```

| type | 名称 | payload | 说明 |
|---|---|---|---|
| `0x10` | 按键 | `u16 vk + u8 pressed` | vk 是 Windows 虚拟键码，查映射表找屏幕坐标 |
| `0x11` | 鼠标移动 | `f32 dx + f32 dy` | 相对位移（PC 像素），按当前指针模式处理 |
| `0x12` | 鼠标按键 | `u8 btn + u8 pressed` | 0 左 1 右 2 中 4 X1 5 X2 |
| `0x13` | 滚轮 | `i16 delta` | 正=上；可绑成虚拟键 `0x88`/`0x89` |
| `0x30` | ping | 无 | 设备收到立刻回 `0x31`，用于测 RTT |
| `0x31` | pong | 无 | |

按键映射表里存的是 **vk 码 → 屏幕坐标 + 半径**：按下时在该点半径内随机取一点注入触摸，抬起时释放。半径内随机是为了避免每次都在同一像素落下。

## 接入内核驱动

这是本仓库刻意留空的唯一部分，也是唯一需要你自己动手的地方。

> **不同驱动，接口不一样。** 按驱动作者给的对接文件（头文件 + 静态库，或协议说明）来。
> 下面的 Paradise 只是一个示例，不是通用规范。

### 四步接入

1. **看本仓库的接口**：`src/driver/driver.h` 的 `IDriver`。
   必须实现：`Init / Release / IsReady / TouchInit / TouchDown / TouchUp`。
   可选（不支持就返回 `false`，上层会自动降级）：陀螺仪、进程控制（隐藏/查进程/模块基址）、内存读写。

2. **看驱动作者的对接文件**：把它的 API 映射到 `IDriver`。
   `src/driver/paradise_driver.cpp` 是一份完整的映射示例，
   `src/driver/stub_driver.cpp` 是最小模板（20 行就能跑）。

3. **注册**：在 `src/driver/stub_driver.cpp` 的 `CreateDriver()` 里加分支，
   然后用 `--driver <你的名字>` 启动。

4. **接不进就先用 stub 联调**：`--driver stub` 不注入但会打日志，
   协议、按键映射、摇杆手感都能先验完，最后再换真驱动。

### 坐标语义

`TouchInit(短边, 长边)`，`TouchDown(slot, x, y)` 里 **x 属于短边域、y 属于长边域**：

- 1080×2400 竖屏机器横屏使用 → `TouchInit(1080, 2400)`
- 上层全部用"横屏逻辑坐标"（原点左上、宽=长边）写业务，
  由 `src/io/injector.cpp` 统一做 90° 旋转 + flip 后再交给驱动

如果你的驱动要的是竖屏原始坐标、或者 X/Y 是反的，**只改 `injector.cpp` 一处**即可。

### Paradise 示例

`src/driver/paradise_driver.cpp` 里已经写好了 `touch_init / touch_down / touch_up /
gyro_update / hide_process / read / write` 的完整映射。启用条件：

- `include/paradise_api.h`（仓库里放了一份原版头文件供参考）
- `libs/<abi>/libparadise_api.a`（由驱动作者提供）

两者都在，CMake 会自动定义 `FH_HAVE_PARADISE_LIB` 并编译进来；缺任意一个就自动退化为
`--driver stub`，不影响其它功能。

## 配置格式

`configs/mappings.example.json` 是可用的最小样例。坐标都是 **横屏逻辑坐标**（原点左上，宽=长边）：

```jsonc
{
  "keys": [                       // 按键映射: PC 键 -> 屏幕点
    { "name": "开火", "keyCode": 1, "x1": 2210, "y1": 900, "radius": 55, "eyeMode": false }
  ],
  "joysticks": [                  // 摇杆: WASD -> 滑动触摸
    { "name": "摇杆1", "cx": 581, "cy": 1523,
      "innerRadius": 92, "middleRadius": 229, "rectHeight": 386,
      "speed": 2597.0, "rangeScale": 1.0, "diagDrift": 20.0, "spawnRadius": 30,
      "keyUp": 87, "keyDown": 83, "keyLeft": 65, "keyRight": 68,
      "sprintKey": 0, "sprintHold": true, "sneakKey": 160, "sneakHold": true }
  ],
  "mouse": {
    "enabled": true, "sensitivity": 2.0, "constrainedSensitivity": 2.0,
    "gyroSensitivity": 2.0, "pointerSlot": 9, "constrainedMode": false,
    "gyroMode": false, "flipX": false, "flipY": false,
    "constrainX1": 600, "constrainY1": 200, "constrainX2": 1800, "constrainY2": 880,
    "modeSwitchKey": 46, "sideBtn1": 0, "sideBtn2": 0
  },
  "keyRadius": 55.0,
  "mouseX": 1200, "mouseY": 540
}
```

`keyCode` 是 Windows 虚拟键码（`1`=鼠标左键，`0x57`=W，见 `src/util/keys.cpp` 的 `KeyName()` 反查）。

## 指针的三种模式

| 模式 | 触发 | 行为 |
|---|---|---|
| 自由 | 默认 | 光标全屏移动，左键按下即触摸按下（可拖动） |
| 约束 | `modeSwitchKey`（默认 Del） | 光标被限制在 `constrainX1..X2 / Y1..Y2` 框内滑动，撞边回中，适合需要持续摇视角的游戏 |
| 陀螺仪 | `gyroSwitchKey` | PC 鼠标位移折算成陀螺仪角速度（需驱动支持 GyroUpdate）；UDP 线程只累加，120Hz 线程按固定频率输出，采样率无关 |

## 维护说明

如需长期维护，请 fork 本仓库后在自己的分支上继续。

## License

MIT，见 `LICENSE`。
