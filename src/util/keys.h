#ifndef FH_UTIL_KEYS_H
#define FH_UTIL_KEYS_H

#include <cstdint>
#include <string>

namespace fh {

// Windows 虚拟键码 -> 可读名 (仅用于日志/配置展示, 映射存的是原始键码)
std::string KeyName(uint16_t vk);

// PC 鼠标按键 -> 合成键码 (0=未映射)。0 左键 1 右键 2 中键 4 X1 5 X2
uint16_t MouseButtonKeyCode(int button);

// 本机第一个非环回 IPv4 (告诉 PC 往哪发)
std::string GetDeviceIP();

// 单调毫秒时钟
int64_t NowMs();

}  // namespace fh

#endif  // FH_UTIL_KEYS_H
