#!/bin/bash

# 创建 build 目录（如果不存在）
mkdir -p build

# 进入 build 目录
cd build

# 生成 Xcode 项目，指定项目名为 AFingerprint
cmake -G Xcode -DPROJECT_NAME=AFingerprint ..

# 打开 Xcode 项目
open AFingerprint.xcodeproj

echo "Xcode project has been generated in the build directory" 