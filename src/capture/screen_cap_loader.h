#ifndef FH_CAPTURE_SCREEN_CAP_LOADER_H
#define FH_CAPTURE_SCREEN_CAP_LOADER_H

namespace fh {

// dlopen libscreen_cap.so (原生 Android16 屏幕捕获, 支持中心裁剪) and control it.
// cropW/cropH: 中心裁剪区域尺寸; dispW/dispH: 屏幕分辨率
bool ScreenStart(int cropW, int cropH, int dispW, int dispH);
void ScreenStop();

// --cap 子进程入口: 干净进程里降权到 shell 并运行采集
void ScreenCapChildEntry(int cropW, int cropH, int dispW, int dispH);

}  // namespace fh

#endif  // FH_CAPTURE_SCREEN_CAP_LOADER_H
