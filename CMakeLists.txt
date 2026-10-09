cmake_minimum_required(VERSION 3.22.1)
project(fenghua_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
#  可选的注入驱动静态库
#
#  本仓库不附带任何内核驱动的对接文件。想接入 Paradise 驱动时, 把这两个文件放好:
#     include/paradise_api.h
#     libs/${ANDROID_ABI}/libparadise_api.a
#  配置阶段检测到就会自动定义 FH_HAVE_PARADISE_LIB, 启用 src/driver/paradise_driver.cpp
#  里的参考实现; 检测不到则只用 stub 驱动, 依然能编译运行 (只是不注入)。
# ---------------------------------------------------------------------------
set(PARADISE_LIB "${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/libparadise_api.a")
set(HAVE_PARADISE FALSE)
if(EXISTS "${PARADISE_LIB}" AND EXISTS "${CMAKE_SOURCE_DIR}/include/paradise_api.h")
    set(HAVE_PARADISE TRUE)
    message(STATUS "Paradise driver lib found -> enabling paradise_driver.cpp")
else()
    message(STATUS "No driver lib at ${PARADISE_LIB} -> building with stub driver only")
endif()

add_executable(fenghua_core
    src/main.cpp
    src/capture/scrcpy_run.cpp
    src/capture/screen_stream.cpp
    src/capture/screen_record.cpp
    src/capture/screen_cap_loader.cpp
    src/core/engine.cpp
    src/driver/stub_driver.cpp
    src/driver/paradise_driver.cpp
    src/io/direct_input.cpp
    src/io/file_logger.cpp
    src/io/injector.cpp
    src/io/joystick_controller.cpp
    src/io/mapping_manager.cpp
    src/io/tcp_server.cpp
    src/io/udp_server.cpp
    src/util/keys.cpp
)

target_include_directories(fenghua_core PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
)

if(HAVE_PARADISE)
    target_compile_definitions(fenghua_core PRIVATE FH_HAVE_PARADISE_LIB)
    target_link_libraries(fenghua_core PRIVATE "${PARADISE_LIB}")
endif()

# 20MB 的内嵌采集库 (libscreen_cap.so) 不进仓库; 想启用就把它生成到
# src/capture/libscreen_cap_embedded.h (见 tools/embed_binary.py),
# 配置阶段检测到即自动定义 FH_HAVE_SCREEN_CAP_LIB 并编译 loader。
if(EXISTS "${CMAKE_SOURCE_DIR}/src/capture/libscreen_cap_embedded.h")
    target_compile_definitions(fenghua_core PRIVATE FH_HAVE_SCREEN_CAP_LIB)
    message(STATUS "libscreen_cap 内嵌库存在 -> 启用 screen_cap_loader")
else()
    message(STATUS "无 libscreen_cap 内嵌库 -> 投屏只用 scrcpy 路径")
endif()

find_library(log-lib log)
# Android 的 pthread 在 libc 里, 不要显式 -lpthread (NDK 没有该 stub)
find_library(dl-lib dl)

# setcon: 采集时降权到 shell 域 (SELinux) 需要。仓库里的预编译库对应设备 /system/lib64。
set(SELINUX_SO "${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/libselinux.so")
if(EXISTS "${SELINUX_SO}")
    target_link_libraries(fenghua_core PRIVATE ${log-lib} ${dl-lib} "${SELINUX_SO}")
    message(STATUS "libselinux.so found -> linking (采集降权到 shell 域用)")
else()
    target_link_libraries(fenghua_core PRIVATE ${log-lib} ${dl-lib})
    message(WARNING "libs/${ANDROID_ABI}/libselinux.so 缺失 -> 投屏采集可能链接失败")
endif()

target_compile_options(fenghua_core PRIVATE
    -O3 -ffunction-sections -fdata-sections
)
set_target_properties(fenghua_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    # json 解析用了异常; 静态链接 libc++ 免得还要单独推 libc++_shared.so
    LINK_FLAGS "-Wl,--gc-sections -s -static-libstdc++"
)
