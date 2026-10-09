# 第三方组件

本仓库包含或依赖以下第三方组件，各自遵循其原始许可证。

## 内嵌的 scrcpy-server.jar

| 项 | 内容 |
|---|---|
| 文件 | `src/capture/scrcpy_server_embedded.h`（jar 的字节数组形式，89,079 字节） |
| 用途 | 设备端屏幕采集：MediaProjection 采集 + MediaCodec H.264 编码 |
| 来源 | [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy) |
| 许可证 | Apache License 2.0 |
| 说明 | 运行时由本程序写到 `/data/local/tmp/scrcpy-server.jar`，再用 `app_process` 拉起 |

Apache-2.0 要求保留版权与许可声明，故在此列出。scrcpy 的完整许可证文本见
<https://github.com/Genymobile/scrcpy/blob/master/LICENSE>。

重新生成这个头文件（例如升级 scrcpy 版本）：

```bash
python tools/embed_binary.py scrcpy-server.jar \
    src/capture/scrcpy_server_embedded.h kScrcpyServerJar "embedded scrcpy-server.jar (Apache-2.0)"
```

## libselinux.so (预编译)

| 项 | 内容 |
|---|---|
| 文件 | `libs/arm64-v8a/libselinux.so` |
| 用途 | 采集时 `setcon` 把自身切到 shell 的 SELinux 域（`u:r:shell:s0`），否则 SurfaceFlinger 拒绝投屏采集 |
| 来源 | Android 系统库（AOSP，Apache License 2.0） |
| 说明 | 与设备 `/system/lib64/libselinux.so` 对应的交叉编译版本；链接期使用，运行时用系统那份 |

## nlohmann/json

| 项 | 内容 |
|---|---|
| 文件 | `src/util/json.hpp` |
| 许可证 | MIT |
| 来源 | <https://github.com/nlohmann/json> |

## 未包含的组件

以下几项**不在本仓库内**，需要你自行准备（详见 README）：

- **内核注入驱动**（如 `libparadise_api.a` + `paradise_api.h`）— 由驱动作者提供，
  接口按 `src/driver/driver.h` 对接
- **可选的采集库**（约 20 MB 的 `libscreen_cap.so`）— 需要时用 `tools/embed_binary.py`
  生成 `src/capture/libscreen_cap_embedded.h`，CMake 会自动启用
