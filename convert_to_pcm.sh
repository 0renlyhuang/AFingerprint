#!/bin/bash

# 检查是否安装了FFmpeg
if ! command -v ffmpeg &> /dev/null; then
    echo "错误: 需要安装FFmpeg"
    echo "可以使用以下命令安装:"
    echo "  MacOS: brew install ffmpeg"
    echo "  Ubuntu: sudo apt-get install ffmpeg"
    exit 1
fi

# 检查参数
if [ "$#" -ne 2 ]; then
    echo "用法: $0 <输入目录> <输出目录>"
    exit 1
fi

INPUT_DIR="$1"
OUTPUT_DIR="$2"

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

# 支持的媒体文件扩展名
EXTENSIONS=("mp3" "mp4" "wav" "m4a" "aac" "ogg" "flac" "wma" "avi" "mkv" "mov")

# 转换函数
convert_to_pcm() {
    local input="$1"
    local output="$2"
    
    echo "正在转换: $input"
    echo "输出到: $output"
    
    ffmpeg -i "$input" \
           -vn \
           -acodec pcm_s16le \
           -ar 44100 \
           -ch_layout mono \
           -f s16le \
           -loglevel warning \
           "$output"
    
    if [ $? -eq 0 ]; then
        echo "转换成功: $input"
    else
        echo "转换失败: $input"
    fi
}

# 计数器
total=0
success=0
failed=0

# 遍历所有支持的文件扩展名
for ext in "${EXTENSIONS[@]}"; do
    # 查找所有匹配的文件（不区分大小写）
    find "$INPUT_DIR" -type f -iname "*.$ext" | while read -r file; do
        ((total++))
        
        # 构建输出文件路径
        relative_path="${file#$INPUT_DIR/}"
        output_file="$OUTPUT_DIR/${relative_path%.*}.pcm"
        
        # 创建输出文件的目录
        mkdir -p "$(dirname "$output_file")"
        
        # 转换文件
        if convert_to_pcm "$file" "$output_file"; then
            ((success++))
        else
            ((failed++))
            echo "警告: 转换失败 - $file"
        fi
        
        echo "----------------------------------------"
    done
done

# 打印统计信息
echo "转换完成!"
echo "总文件数: $total"
echo "成功: $success"
echo "失败: $failed"