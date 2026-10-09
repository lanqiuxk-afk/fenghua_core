#ifndef FH_CAPTURE_SCRCPY_RUN_H
#define FH_CAPTURE_SCRCPY_RUN_H

namespace fh {

// 内嵌 scrcpy-server.jar, 用 app_process 在降权后的 shell 域运行,
// 走 MediaProjection 采集 + MediaCodec 编码, 视频流经 localabstract 回连本地,
// 再由 screen_stream 转发到 TCP 56790 给 PC。
//
// cropW/cropH: 采集区域(中心裁剪); dispW/dispH: 屏幕尺寸。
// 传全屏尺寸即全屏采集。
bool ScrcpyStart(int cropW, int cropH, int dispW, int dispH);
void ScrcpyStop();

}  // namespace fh

#endif  // FH_CAPTURE_SCRCPY_RUN_H
