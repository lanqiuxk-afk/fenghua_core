#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把任意二进制文件转成 C 头文件里的字节数组。

用法:
    python tools/embed_binary.py <输入文件> <输出头文件> <数组名> [头注释]

例:
    python tools/embed_binary.py scrcpy-server.jar \
        src/capture/scrcpy_server_embedded.h kScrcpyServerJar \
        "embedded scrcpy-server.jar (Apache-2.0)"

生成的变量:
    <数组名>[]      字节数组
    <数组名>Len     长度 (unsigned int)
"""
import io
import sys
import time


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    src, dst, name = sys.argv[1], sys.argv[2], sys.argv[3]
    comment = sys.argv[4] if len(sys.argv) > 4 else ""

    data = io.open(src, "rb").read()
    lines = [
        "#pragma once",
        "//",
        "//  自动生成, 请勿手改。",
        "//  来源: %s" % src,
    ]
    if comment:
        lines.append("//  说明: %s" % comment)
    lines += [
        "//  大小: %d 字节" % len(data),
        "//  生成时间: %s" % time.strftime("%Y-%m-%d %H:%M:%S"),
        "//",
        "",
        "static const unsigned char %s[] = {" % name,
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join("0x%02x," % b for b in data[i:i + 16]))
    lines += [
        "};",
        "static const unsigned int %sLen = %d;" % (name, len(data)),
        "",
    ]
    io.open(dst, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print("生成 %s (%d 字节 -> %d 行)" % (dst, len(data), len(lines)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
