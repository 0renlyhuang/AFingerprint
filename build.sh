#!/bin/bash

# 创建并进入 build 目录
mkdir -p build
cd build

# 清理之前的构建缓存
rm -rf CMakeCache.txt CMakeFiles

# 生成构建文件并编译
cmake -G "Unix Makefiles" .. && cmake --build .

# 如果编译成功，打印成功信息
if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
else
    echo "Build failed!"
    exit 1
fi 