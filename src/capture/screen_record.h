#ifndef FH_CAPTURE_SCREEN_RECORD_H
#define FH_CAPTURE_SCREEN_RECORD_H

namespace fh {

// 基于系统 screenrecord 的投屏采集 (root, 无需 APK / MediaProjection 授权)
// 通过 FIFO 读取 screenrecord 的 H.264 输出并推送到 56790 转发器
bool StartScreenRecord(int width, int height);
void StopScreenRecord();

}  // namespace fh

#endif  // FH_CAPTURE_SCREEN_RECORD_H
