#!/bin/bash

# 创建 build 目录（如果不存在）
mkdir -p build

# 进入 build 目录
cd build

# 清理旧的构建缓存（可选）
rm -rf CMakeCache.txt CMakeFiles/

# 生成 Xcode 项目，指定为静态库构建
cmake -G Xcode -DPROJECT_NAME=AFingerprint -DBUILD_SHARED_LIBS=OFF ..

# 配置项目生成成果物位置
# 可以在此处添加额外的CMake设置，如 -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=../lib

# 打开 Xcode 项目
open AFingerprint.xcodeproj

echo "Xcode project has been generated in the build directory"
echo "The static library will be built at: build/src/libafp/Debug/libafp.a (Debug configuration)"
echo "or: build/src/libafp/Release/libafp.a (Release configuration)"
echo ""
echo "After building, XCFramework will be created at: build/xcframework/libafp.xcframework"
echo "通过在Xcode中构建项目来创建xcframework" 