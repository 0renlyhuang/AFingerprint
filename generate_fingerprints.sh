# 获取test_pcm目录下的所有PCM文件
PCM_FILES=$(find train_pcm -name "*.pcm" -type f)

# 如果没有找到文件，则退出
if [ -z "$PCM_FILES" ]; then
    echo "错误：在train_pcm目录下未找到PCM文件"
    exit 1
fi

# 构建命令行参数
CMD="./build/AFingerprint generate shazam fingerprints.db"

# 添加所有找到的PCM文件作为输入参数
for file in $PCM_FILES; do
    CMD="$CMD $file"
done

# 执行命令
echo "执行命令: $CMD"
$CMD