#ifndef FH_CAPTURE_SCREEN_STREAM_H
#define FH_CAPTURE_SCREEN_STREAM_H

#include <cstddef>
#include <cstdint>

#include "io/mapping_manager.h"

namespace fh {

// 投屏: 监听 56790, 把设备端采集到的 H.264 转发给 PC
//
// 线上格式 (PC 端 fenghua_sender 按此解析):
//   [16B 流头] "FHSC" + w(u32BE) + h(u32BE) + u32 flags
//   之后每个访问单元: [ptsAndFlags 8B][packetSize u32BE][payload]
//
// 配置读写 (可选): 把投屏开关/尺寸落到 screen.json
void WriteScreenConfig(const ScreenConfig& sc);
void WriteScreenConfigAsync(const ScreenConfig& sc);

void StartScreenServer();
void StopScreenServer();

// 新采集会话开始: 清空前缀缓存 (PC 中途连入时先补发流头)
void ScreenStreamBegin();
// 推送一个包给所有 PC 客户端; 中途连入的先收到流头, 再等下一个完整包
void ScreenStreamPush(const uint8_t* data, size_t len);

}  // namespace fh

#endif  // FH_CAPTURE_SCREEN_STREAM_H
