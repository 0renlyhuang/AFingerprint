#!/bin/bash

# This script runs the visualization tools for audio fingerprinting

# Create visualization directory if it doesn't exist
VISUALIZATION_DIR="visualizations"
mkdir -p $VISUALIZATION_DIR

# Check for Python and required modules
echo "Checking Python and required modules..."
python3 -c "import matplotlib" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Installing matplotlib..."
    pip3 install matplotlib
fi

# Check for audio playback modules
echo "Checking audio playback modules..."
python3 -c "import soundfile, sounddevice" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Installing soundfile and sounddevice for audio playback..."
    pip3 install soundfile sounddevice
fi

# Make sure Python visualization script exists
VISUALIZATION_SCRIPT="src/visualization/visualize_fingerprints.py"
if [ ! -f "$VISUALIZATION_SCRIPT" ]; then
    echo "Error: Visualization script not found at $VISUALIZATION_SCRIPT"
    exit 1
fi

# 检查Python是否可以正确导入必要模块
echo "检查Python可视化脚本..."
python3 -c "import sys; print('Python解释器工作正常'); sys.path.append('src/visualization'); import visualize_fingerprints; print('可视化脚本导入成功')" || {
    echo "警告: 无法导入可视化脚本。请确保Python环境正确配置。"
    echo "尝试运行: python3 $VISUALIZATION_SCRIPT --help"
}

# Make sure script is executable
chmod +x $VISUALIZATION_SCRIPT

# Locate all JSON fingerprint files
EXTRACTION_FILES=$(find . -name "*_fingerprint.json")
QUERY_FILES=$(find . -name "*_query.json")
SOURCE_FILES=$(find . -name "*_source.json")
SESSION_FILES=$(find . -name "*_sessions.json")

# Locate all audio files
AUDIO_FILES=$(find . -name "*.wav" -o -name "*.mp3" -o -name "*.flac" -o -name "*.ogg" -o -name "*.pcm")

echo "Found:"
echo "  - Extraction data files: $(echo $EXTRACTION_FILES | wc -w)"
echo "  - Query data files: $(echo $QUERY_FILES | wc -w)"
echo "  - Source data files: $(echo $SOURCE_FILES | wc -w)"
echo "  - Session data files: $(echo $SESSION_FILES | wc -w)"
echo "  - Audio files: $(echo $AUDIO_FILES | wc -w)"

# Function to check if a JSON file has an audio path
function check_json_audio_path() {
    local json_file="$1"
    # 使用更简单的grep方式，避免sed中的路径问题
    local audio_path=$(grep -o '"audioFilePath": *"[^"]*"' "$json_file" | cut -d'"' -f4)
    
    if [ -n "$audio_path" ]; then
        # 移除可能的前缀和换行符
        audio_path=$(echo "$audio_path" | sed 's/^audio_path: //g' | tr -d '\n')
        
        # Check if file exists as is
        if [ -f "$audio_path" ]; then
            echo "$audio_path"
        # Check if path is relative to project root
        elif [ -f "./$audio_path" ]; then
            echo "./$audio_path"
        else
            echo ""
        fi
    else
        echo ""
    fi
}

# Function to choose between interactive and static mode
function show_menu() {
    echo ""
    echo "选择可视化模式："
    echo "1. 生成静态图像 (生成 PNG 文件)"
    echo "2. 启动交互式可视化 (支持悬停查看详情)"
    echo "3. 启动带音频播放的可视化 (支持播放音频文件)"
    echo ""
    read -p "请选择 (1/2/3): " mode_choice
    
    if [ "$mode_choice" == "1" ]; then
        generate_static_images
    elif [ "$mode_choice" == "2" ]; then
        launch_interactive_visualization
    elif [ "$mode_choice" == "3" ]; then
        launch_audio_playback_visualization
    else
        echo "无效选择，默认使用静态模式"
        generate_static_images
    fi
}

# Function to generate static images
function generate_static_images() {
    echo "正在生成静态图像..."
    
    # Process extraction visualization files
    for file in $EXTRACTION_FILES; do
        echo "处理提取可视化: $file"
        output_file="${VISUALIZATION_DIR}/$(basename ${file%.json}).png"
        python3 $VISUALIZATION_SCRIPT --source "$file" --output "$output_file"
    done

    # Process matching visualization files (single query files)
    for file in $QUERY_FILES; do
        echo "处理查询可视化: $file"
        output_file="${VISUALIZATION_DIR}/$(basename ${file%.json}).png"
        python3 $VISUALIZATION_SCRIPT --query "$file" --output "$output_file"
    done

    # Process comparison visualization files (source + query + sessions)
    for source_file in $SOURCE_FILES; do
        # Extract base name to find matching query and session files
        base_name=$(basename $source_file _source.json)
        query_file="${source_file%_source.json}_query.json"
        session_file="${source_file%_source.json}_sessions.json"
        
        if [ -f "$query_file" ]; then
            echo "处理比较可视化: $base_name"
            output_file="${VISUALIZATION_DIR}/${base_name}_comparison.png"
            
            # Check if session file exists
            if [ -f "$session_file" ]; then
                python3 $VISUALIZATION_SCRIPT --source "$source_file" --query "$query_file" --sessions "$session_file" --output "$output_file"
            else
                python3 $VISUALIZATION_SCRIPT --source "$source_file" --query "$query_file" --output "$output_file"
            fi
        fi
    done

    echo "所有静态图像已生成在 ${VISUALIZATION_DIR} 目录"
}

# Function to prompt for audio file selection
function select_audio_file() {
    local prompt="$1"
    local default_file="$2"
    
    # If there's a default file from JSON, use it automatically without asking
    if [ -n "$default_file" ]; then
        echo "JSON 文件中包含音频路径, 使用: $(basename "$default_file")"
        echo "$default_file"
        return
    fi
    
    # Check if there are any audio files
    if [ -z "$AUDIO_FILES" ]; then
        echo "No audio files found"
        return
    fi
    
    # Create a menu of audio files
    echo "$prompt"
    echo "0) 不使用音频文件"
    
    local i=1
    local audio_file_array=()
    
    for file in $AUDIO_FILES; do
        audio_file_array+=("$file")
        echo "$i) $(basename "$file")"
        i=$((i+1))
    done
    
    read -p "请选择一个音频文件 (0-$((i-1))): " audio_choice
    
    # If valid choice, return the file path
    if [ "$audio_choice" -ge 1 ] && [ "$audio_choice" -le "$((i-1))" ]; then
        echo "${audio_file_array[$((audio_choice-1))]}"
    else
        echo ""
    fi
}

# Function to launch interactive visualization
function launch_interactive_visualization() {
    echo "启动交互式可视化..."
    
    # Gather all visualization combinations
    visualizations=()
    
    # Add all extraction visualizations
    for file in $EXTRACTION_FILES; do
        # Check if JSON has audio file path
        json_audio_path=$(check_json_audio_path "$file")
        # 显示固定的文件名，不要括号
        if [ -n "$json_audio_path" ]; then
            visualizations+=("提取可视化 (带嵌入音频: $(basename "$json_audio_path")) - source $file")
        else
            visualizations+=("提取可视化: $(basename "$file") - source $file")
        fi
    done
    
    # Add all query visualizations
    for file in $QUERY_FILES; do
        # Check if JSON has audio file path
        json_audio_path=$(check_json_audio_path "$file")
        if [ -n "$json_audio_path" ]; then
            visualizations+=("查询可视化 (带嵌入音频: $(basename "$json_audio_path")) - query $file")
        else
            visualizations+=("查询可视化: $(basename "$file") - query $file")
        fi
    done
    
    # Add all comparison visualizations
    for source_file in $SOURCE_FILES; do
        # Extract base name to find matching query and session files
        base_name=$(basename $source_file _source.json)
        query_file="${source_file%_source.json}_query.json"
        session_file="${source_file%_source.json}_sessions.json"
        
        if [ -f "$query_file" ]; then
            if [ -f "$session_file" ]; then
                visualizations+=("完整比较可视化: $base_name - python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --query \"$query_file\" --sessions \"$session_file\"")
            else
                visualizations+=("基础比较可视化: $base_name - python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --query \"$query_file\"")
            fi
        fi
    done
    
    # Show menu if we have visualizations
    if [ ${#visualizations[@]} -eq 0 ]; then
        echo "没有找到可视化文件。请先运行程序并使用 --visualize 标志生成可视化数据。"
        exit 1
    fi
    
    # Display visualization options
    echo "可用的可视化选项:"
    for i in "${!visualizations[@]}"; do
        echo "$((i+1)). ${visualizations[$i]}"
    done
    
    echo ""
    echo "0. 退出"
    echo ""
    
    # Loop to allow multiple visualizations
    while true; do
        read -p "请选择要查看的可视化 (0-${#visualizations[@]}): " viz_choice
        
        if [ "$viz_choice" == "0" ]; then
            echo "退出可视化工具"
            break
        elif [ "$viz_choice" -ge 1 ] && [ "$viz_choice" -le "${#visualizations[@]}" ]; then
            # Extract visualization type and files
            viz_info="${visualizations[$((viz_choice-1))]}"
            echo "选择的可视化: $viz_info"
            viz_type=$(echo "$viz_info" | sed 's/.*- \(.*\)/\1/')
            echo "解析的可视化类型: $viz_type"
            
            # Handle different visualization types
            if [[ "$viz_type" == "source"* ]]; then
                echo "检测到源文件可视化类型"
                # 直接从viz_type中提取完整文件路径，避免basename问题
                IFS=' ' read -r -a parts <<< "$viz_type"
                echo "解析的部分: ${parts[*]}"
                source_file="${parts[1]}"
                echo "提取的源文件路径: $source_file"
                
                # 确保文件存在
                if [ ! -f "$source_file" ]; then
                    echo "错误: 源文件不存在: $source_file"
                    continue
                fi
                
                # Check if JSON has audio file path
                echo "正在检查JSON文件中的音频路径..."
                json_audio_path=$(check_json_audio_path "$source_file")
                echo "从JSON中提取的音频路径: '$json_audio_path'"
                
                # 检查路径是否存在特殊字符
                echo "音频路径字符分析:"
                echo "- 路径长度: ${#json_audio_path}"
                echo "- 十六进制表示: $(echo -n "$json_audio_path" | xxd -p)"
                
                # Automatically use JSON audio path if available, otherwise prompt
                if [ -n "$json_audio_path" ]; then
                    # 确保路径清洁，移除可能的引号和空格
                    json_audio_path=$(echo "$json_audio_path" | tr -d '"' | xargs)
                    echo "清洗后的路径: '$json_audio_path'"
                    
                    cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --source-audio \"$json_audio_path\""
                    echo "运行: $cmd (使用JSON中嵌入的音频路径: $json_audio_path)"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                    echo "- 音频文件: $([ -f "$json_audio_path" ] && echo "存在" || echo "不存在")"
                else
                    # Prompt for audio file only if JSON doesn't have a path
                    audio_file=$(select_audio_file "请为源文件选择一个音频文件:" "")
                    
                    # Run visualization
                    if [ -n "$audio_file" ]; then
                        cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --source-audio \"$audio_file\""
                    else
                        cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\""
                    fi
                    echo "运行: $cmd"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                    if [ -n "$audio_file" ]; then
                        echo "- 音频文件: $([ -f "$audio_file" ] && echo "存在" || echo "不存在")"
                    fi
                fi
                
                # 使用set -x显示命令执行过程
                echo "显示命令执行过程..."
                set -x
                # 捕获标准输出和错误输出
                OUTPUT=$(eval "$cmd" 2>&1)
                EXIT_CODE=$?
                set +x
                echo "命令执行完成，退出代码: $EXIT_CODE"
                
                # 显示输出结果
                if [ $EXIT_CODE -ne 0 ]; then
                    echo "错误: 命令执行失败！"
                    echo "错误详情:"
                    echo "$OUTPUT"
                else
                    echo "命令执行成功"
                    if [ -n "$OUTPUT" ]; then
                        echo "输出内容:"
                        echo "$OUTPUT"
                    else
                        echo "没有输出内容"
                    fi
                fi
                
            elif [[ "$viz_type" == "query"* ]]; then
                echo "检测到查询文件可视化类型"
                # 直接从viz_type中提取完整文件路径，避免basename问题
                IFS=' ' read -r -a parts <<< "$viz_type"
                echo "解析的部分: ${parts[*]}"
                query_file="${parts[1]}"
                echo "提取的查询文件路径: $query_file"
                
                # 确保文件存在
                if [ ! -f "$query_file" ]; then
                    echo "错误: 查询文件不存在: $query_file"
                    continue
                fi
                
                # Check if JSON has audio file path
                echo "正在检查JSON文件中的音频路径..."
                json_audio_path=$(check_json_audio_path "$query_file")
                echo "从JSON中提取的音频路径: '$json_audio_path'"
                
                # 检查路径是否存在特殊字符
                echo "音频路径字符分析:"
                echo "- 路径长度: ${#json_audio_path}"
                echo "- 十六进制表示: $(echo -n "$json_audio_path" | xxd -p)"
                
                # Automatically use JSON audio path if available, otherwise prompt
                if [ -n "$json_audio_path" ]; then
                    # 确保路径清洁，移除可能的引号和空格
                    json_audio_path=$(echo "$json_audio_path" | tr -d '"' | xargs)
                    echo "清洗后的路径: '$json_audio_path'"
                    
                    cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\" --query-audio \"$json_audio_path\""
                    echo "运行: $cmd (使用JSON中嵌入的音频路径: $json_audio_path)"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                    echo "- 音频文件: $([ -f "$json_audio_path" ] && echo "存在" || echo "不存在")"
                else
                    # Prompt for audio file only if JSON doesn't have a path
                    audio_file=$(select_audio_file "请为查询文件选择一个音频文件:" "")
                    
                    # Run visualization
                    if [ -n "$audio_file" ]; then
                        cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\" --query-audio \"$audio_file\""
                    else
                        cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\""
                    fi
                    echo "运行: $cmd"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                    if [ -n "$audio_file" ]; then
                        echo "- 音频文件: $([ -f "$audio_file" ] && echo "存在" || echo "不存在")"
                    fi
                fi
                
                # 使用set -x显示命令执行过程
                echo "显示命令执行过程..."
                set -x
                # 捕获标准输出和错误输出
                OUTPUT=$(eval "$cmd" 2>&1)
                EXIT_CODE=$?
                set +x
                echo "命令执行完成，退出代码: $EXIT_CODE"
                
                # 显示输出结果
                if [ $EXIT_CODE -ne 0 ]; then
                    echo "错误: 命令执行失败！"
                    echo "错误详情:"
                    echo "$OUTPUT"
                else
                    echo "命令执行成功"
                    if [ -n "$OUTPUT" ]; then
                        echo "输出内容:"
                        echo "$OUTPUT"
                    else
                        echo "没有输出内容"
                    fi
                fi
            elif [[ "$viz_type" == comparison* ]]; then
                echo "检测到比较可视化类型"
                # Extract comparison files
                IFS=' ' read -r -a files <<< "$viz_type"
                echo "解析的部分: ${files[*]}"
                source_file="${files[1]}"
                query_file="${files[2]}"
                session_file="${files[3]:-}"
                echo "提取的路径信息:"
                echo "- 源文件: $source_file"
                echo "- 查询文件: $query_file"
                echo "- 会话文件: ${session_file:-无}"
                
                # 确保文件存在
                if [ ! -f "$source_file" ]; then
                    echo "错误: 源文件不存在: $source_file"
                    continue
                fi
                if [ ! -f "$query_file" ]; then
                    echo "错误: 查询文件不存在: $query_file"
                    continue
                fi
                if [ -n "$session_file" ] && [ ! -f "$session_file" ]; then
                    echo "警告: 会话文件不存在: $session_file"
                    session_file=""
                fi
                
                # Check if any of the files have embedded audio paths
                echo "正在检查JSON文件中的音频路径..."
                source_audio=$(check_json_audio_path "$source_file")
                query_audio=$(check_json_audio_path "$query_file")
                echo "从JSON中提取的音频路径:"
                echo "- 源文件: '$source_audio'"
                echo "- 查询文件: '$query_audio'"
                
                if [ -n "$source_audio" ] || [ -n "$query_audio" ]; then
                    echo "检测到JSON文件中已嵌入音频路径:"
                    [ -n "$source_audio" ] && echo "- 源文件: $(basename "$source_audio")"
                    [ -n "$query_audio" ] && echo "- 查询文件: $(basename "$query_audio")"
                    echo "注意: 比较可视化暂不支持音频播放，但脚本会正确读取JSON中的音频路径用于单个文件的可视化"
                fi
                
                # Run visualization
                if [ -n "$session_file" ]; then
                    cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --source \"$source_file\" --query \"$query_file\" --sessions \"$session_file\""
                else
                    cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --source \"$source_file\" --query \"$query_file\""
                fi
                
                echo "运行比较可视化命令: $cmd"
                echo "检查文件是否存在:"
                echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                if [ -n "$session_file" ]; then
                    echo "- 会话文件: $([ -f "$session_file" ] && echo "存在" || echo "不存在")"
                fi
                
                # 直接执行而不捕获输出，确保能看到所有输出
                echo "============ 直接执行比较可视化 ============"
                echo "时间戳: $(date)"
                echo "命令: $cmd"
                eval "$cmd"
                exit_code=$?
                echo "============ 执行完成，退出代码: $exit_code ============"
                
                if [ $exit_code -ne 0 ]; then
                    echo "执行失败，尝试不同的后端..."
                    # 尝试不同的后端
                    for backend in "TkAgg" "Qt5Agg" "macosx"; do
                        echo "尝试使用 $backend 后端..."
                        backend_cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --force-backend $backend --source \"$source_file\" --query \"$query_file\""
                        if [ -n "$session_file" ]; then
                            backend_cmd="$backend_cmd --sessions \"$session_file\""
                        fi
                        echo "命令: $backend_cmd"
                        eval "$backend_cmd"
                        if [ $? -eq 0 ]; then
                            echo "$backend 后端执行成功"
                            break
                        fi
                    done
                fi
            else
                echo "未知可视化类型: $viz_type"
            fi
        else
            echo "无效选择，请重试"
        fi
    done
}

# Function to launch audio playback visualization
function launch_audio_playback_visualization() {
    echo "启动带音频播放的可视化..."
    
    # Gather all visualization combinations
    visualizations=()
    
    # Add all extraction visualizations
    for file in $EXTRACTION_FILES; do
        # Check if JSON has audio file path
        json_audio_path=$(check_json_audio_path "$file")
        if [ -n "$json_audio_path" ]; then
            visualizations+=("提取可视化 (带嵌入音频: $(basename "$json_audio_path")) - source $file")
        else
            visualizations+=("提取可视化: $(basename $file) - source $file")
        fi
    done
    
    # Add all query visualizations
    for file in $QUERY_FILES; do
        # Check if JSON has audio file path
        json_audio_path=$(check_json_audio_path "$file")
        if [ -n "$json_audio_path" ]; then
            visualizations+=("查询可视化 (带嵌入音频: $(basename "$json_audio_path")) - query $file")
        else
            visualizations+=("查询可视化: $(basename $file) - query $file")
        fi
    done
    
    # Add all comparison visualizations
    for source_file in $SOURCE_FILES; do
        # Extract base name to find matching query and session files
        base_name=$(basename $source_file _source.json)
        query_file="${source_file%_source.json}_query.json"
        session_file="${source_file%_source.json}_sessions.json"
        
        if [ -f "$query_file" ]; then
            # Check if source and query have audio paths
            source_audio=$(check_json_audio_path "$source_file")
            query_audio=$(check_json_audio_path "$query_file")
            
            if [ -n "$source_audio" ] && [ -n "$query_audio" ]; then
                visualizations+=("完整比较可视化 (带嵌入音频) - comparison $source_file $query_file $session_file")
            elif [ -n "$source_audio" ]; then
                visualizations+=("完整比较可视化 (源带嵌入音频) - comparison $source_file $query_file $session_file")
            elif [ -n "$query_audio" ]; then
                visualizations+=("完整比较可视化 (查询带嵌入音频) - comparison $source_file $query_file $session_file")
            elif [ -f "$session_file" ]; then
                visualizations+=("完整比较可视化 - comparison $source_file $query_file $session_file")
            else
                visualizations+=("基础比较可视化 - comparison $source_file $query_file")
            fi
        fi
    done
    
    # Show menu if we have visualizations
    if [ ${#visualizations[@]} -eq 0 ]; then
        echo "没有找到可视化文件。请先运行程序并使用 --visualize 标志生成可视化数据。"
        exit 1
    fi
    
    # Display visualization options
    echo "可用的可视化选项:"
    for i in "${!visualizations[@]}"; do
        echo "$((i+1)). ${visualizations[$i]}"
    done
    
    echo ""
    echo "0. 退出"
    echo ""
    
    # Loop to allow multiple visualizations
    while true; do
        read -p "请选择要查看的可视化 (0-${#visualizations[@]}): " viz_choice
        
        if [ "$viz_choice" == "0" ]; then
            echo "退出可视化工具"
            break
        elif [ "$viz_choice" -ge 1 ] && [ "$viz_choice" -le "${#visualizations[@]}" ]; then
            # Extract visualization type and files
            viz_info="${visualizations[$((viz_choice-1))]}"
            echo "选择的可视化: $viz_info"
            viz_type=$(echo "$viz_info" | sed 's/.*- \(.*\)/\1/')
            echo "解析的可视化类型: $viz_type"
            
            # Handle different visualization types
            if [[ "$viz_type" == "source"* ]]; then
                echo "检测到源文件可视化类型"
                # 直接从viz_type中提取完整文件路径，避免basename问题
                IFS=' ' read -r -a parts <<< "$viz_type"
                echo "解析的部分: ${parts[*]}"
                source_file="${parts[1]}"
                echo "提取的源文件路径: $source_file"
                
                # 确保文件存在
                if [ ! -f "$source_file" ]; then
                    echo "错误: 源文件不存在: $source_file"
                    continue
                fi
                
                # Check if JSON has audio file path
                echo "正在检查JSON文件中的音频路径..."
                json_audio_path=$(check_json_audio_path "$source_file")
                echo "从JSON中提取的音频路径: '$json_audio_path'"
                
                # 检查路径是否存在特殊字符
                echo "音频路径字符分析:"
                echo "- 路径长度: ${#json_audio_path}"
                echo "- 十六进制表示: $(echo -n "$json_audio_path" | xxd -p)"
                
                # Automatically use JSON audio path if available, otherwise prompt
                if [ -n "$json_audio_path" ]; then
                    # 确保路径清洁，移除可能的引号和空格
                    json_audio_path=$(echo "$json_audio_path" | tr -d '"' | xargs)
                    echo "清洗后的路径: '$json_audio_path'"
                    
                    cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --source-audio \"$json_audio_path\""
                    echo "运行: $cmd (使用JSON中嵌入的音频路径: $json_audio_path)"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                    echo "- 音频文件: $([ -f "$json_audio_path" ] && echo "存在" || echo "不存在")"
                else
                    # Prompt for audio file only if JSON doesn't have a path
                    audio_file=$(select_audio_file "请为源文件选择一个音频文件:" "")
                    
                    # Run visualization
                    if [ -n "$audio_file" ]; then
                        cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\" --source-audio \"$audio_file\""
                    else
                        cmd="python3 $VISUALIZATION_SCRIPT --source \"$source_file\""
                    fi
                    echo "运行: $cmd"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                    if [ -n "$audio_file" ]; then
                        echo "- 音频文件: $([ -f "$audio_file" ] && echo "存在" || echo "不存在")"
                    fi
                fi
                
                # 使用set -x显示命令执行过程
                echo "显示命令执行过程..."
                set -x
                # 捕获标准输出和错误输出
                OUTPUT=$(eval "$cmd" 2>&1)
                EXIT_CODE=$?
                set +x
                echo "命令执行完成，退出代码: $EXIT_CODE"
                
                # 显示输出结果
                if [ $EXIT_CODE -ne 0 ]; then
                    echo "错误: 命令执行失败！"
                    echo "错误详情:"
                    echo "$OUTPUT"
                else
                    echo "命令执行成功"
                    if [ -n "$OUTPUT" ]; then
                        echo "输出内容:"
                        echo "$OUTPUT"
                    else
                        echo "没有输出内容"
                    fi
                fi
                
            elif [[ "$viz_type" == "query"* ]]; then
                echo "检测到查询文件可视化类型"
                # 直接从viz_type中提取完整文件路径，避免basename问题
                IFS=' ' read -r -a parts <<< "$viz_type"
                echo "解析的部分: ${parts[*]}"
                query_file="${parts[1]}"
                echo "提取的查询文件路径: $query_file"
                
                # 确保文件存在
                if [ ! -f "$query_file" ]; then
                    echo "错误: 查询文件不存在: $query_file"
                    continue
                fi
                
                # Check if JSON has audio file path
                echo "正在检查JSON文件中的音频路径..."
                json_audio_path=$(check_json_audio_path "$query_file")
                echo "从JSON中提取的音频路径: '$json_audio_path'"
                
                # 检查路径是否存在特殊字符
                echo "音频路径字符分析:"
                echo "- 路径长度: ${#json_audio_path}"
                echo "- 十六进制表示: $(echo -n "$json_audio_path" | xxd -p)"
                
                # Automatically use JSON audio path if available, otherwise prompt
                if [ -n "$json_audio_path" ]; then
                    # 确保路径清洁，移除可能的引号和空格
                    json_audio_path=$(echo "$json_audio_path" | tr -d '"' | xargs)
                    echo "清洗后的路径: '$json_audio_path'"
                    
                    cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\" --query-audio \"$json_audio_path\""
                    echo "运行: $cmd (使用JSON中嵌入的音频路径: $json_audio_path)"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                    echo "- 音频文件: $([ -f "$json_audio_path" ] && echo "存在" || echo "不存在")"
                else
                    # Prompt for audio file only if JSON doesn't have a path
                    audio_file=$(select_audio_file "请为查询文件选择一个音频文件:" "")
                    
                    # Run visualization
                    if [ -n "$audio_file" ]; then
                        cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\" --query-audio \"$audio_file\""
                    else
                        cmd="python3 $VISUALIZATION_SCRIPT --query \"$query_file\""
                    fi
                    echo "运行: $cmd"
                    echo "检查文件是否存在:"
                    echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                    echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                    if [ -n "$audio_file" ]; then
                        echo "- 音频文件: $([ -f "$audio_file" ] && echo "存在" || echo "不存在")"
                    fi
                fi
                
                # 使用set -x显示命令执行过程
                echo "显示命令执行过程..."
                set -x
                # 捕获标准输出和错误输出
                OUTPUT=$(eval "$cmd" 2>&1)
                EXIT_CODE=$?
                set +x
                echo "命令执行完成，退出代码: $EXIT_CODE"
                
                # 显示输出结果
                if [ $EXIT_CODE -ne 0 ]; then
                    echo "错误: 命令执行失败！"
                    echo "错误详情:"
                    echo "$OUTPUT"
                else
                    echo "命令执行成功"
                    if [ -n "$OUTPUT" ]; then
                        echo "输出内容:"
                        echo "$OUTPUT"
                    else
                        echo "没有输出内容"
                    fi
                fi
            elif [[ "$viz_type" == comparison* ]]; then
                echo "检测到比较可视化类型"
                # Extract comparison files
                IFS=' ' read -r -a files <<< "$viz_type"
                echo "解析的部分: ${files[*]}"
                source_file="${files[1]}"
                query_file="${files[2]}"
                session_file="${files[3]:-}"
                echo "提取的路径信息:"
                echo "- 源文件: $source_file"
                echo "- 查询文件: $query_file"
                echo "- 会话文件: ${session_file:-无}"
                
                # 确保文件存在
                if [ ! -f "$source_file" ]; then
                    echo "错误: 源文件不存在: $source_file"
                    continue
                fi
                if [ ! -f "$query_file" ]; then
                    echo "错误: 查询文件不存在: $query_file"
                    continue
                fi
                if [ -n "$session_file" ] && [ ! -f "$session_file" ]; then
                    echo "警告: 会话文件不存在: $session_file"
                    session_file=""
                fi
                
                # Check if any of the files have embedded audio paths
                echo "正在检查JSON文件中的音频路径..."
                source_audio=$(check_json_audio_path "$source_file")
                query_audio=$(check_json_audio_path "$query_file")
                echo "从JSON中提取的音频路径:"
                echo "- 源文件: '$source_audio'"
                echo "- 查询文件: '$query_audio'"
                
                # Run visualization
                if [ -n "$session_file" ]; then
                    cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --source \"$source_file\" --query \"$query_file\" --sessions \"$session_file\""
                else
                    cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --source \"$source_file\" --query \"$query_file\""
                fi
                
                echo "运行: $cmd"
                echo "检查文件是否存在:"
                echo "- 可视化脚本: $([ -f "$VISUALIZATION_SCRIPT" ] && echo "存在" || echo "不存在")"
                echo "- 源文件: $([ -f "$source_file" ] && echo "存在" || echo "不存在")"
                echo "- 查询文件: $([ -f "$query_file" ] && echo "存在" || echo "不存在")"
                if [ -n "$session_file" ]; then
                    echo "- 会话文件: $([ -f "$session_file" ] && echo "存在" || echo "不存在")"
                fi
                
                # 直接执行而不捕获输出，确保能看到所有输出
                echo "============ 直接执行比较可视化 ============"
                echo "时间戳: $(date)"
                echo "命令: $cmd"
                eval "$cmd"
                exit_code=$?
                echo "============ 执行完成，退出代码: $exit_code ============"
                
                if [ $exit_code -ne 0 ]; then
                    echo "执行失败，尝试不同的后端..."
                    # 尝试不同的后端
                    for backend in "TkAgg" "Qt5Agg" "macosx"; do
                        echo "尝试使用 $backend 后端..."
                        backend_cmd="python3 $VISUALIZATION_SCRIPT --debug-comparison --force-backend $backend --source \"$source_file\" --query \"$query_file\""
                        if [ -n "$session_file" ]; then
                            backend_cmd="$backend_cmd --sessions \"$session_file\""
                        fi
                        echo "命令: $backend_cmd"
                        eval "$backend_cmd"
                        if [ $? -eq 0 ]; then
                            echo "$backend 后端执行成功"
                            break
                        fi
                    done
                fi
            else
                echo "未知可视化类型: $viz_type"
            fi
        else
            echo "无效选择，请重试"
        fi
    done
}

# Main script execution
if [ -t 0 ]; then  # Check if script is running interactively
    # 测试直接运行查询可视化
    if [ "$1" == "test" ]; then
        echo "执行可视化测试..."
        QUERY_FILE="./build/Debug/v_s_35_query.json"
        AUDIO_FILE="./test_pcm/v_s_35.pcm"
        
        if [ -f "$QUERY_FILE" ] && [ -f "$AUDIO_FILE" ]; then
            echo "测试文件都存在，直接运行可视化"
            # 检查文件内容
            echo "JSON文件内容摘要:"
            grep "audioFilePath" "$QUERY_FILE" || echo "未找到audioFilePath"
            
            # 直接使用相对路径运行
            echo "运行: python3 $VISUALIZATION_SCRIPT --query \"$QUERY_FILE\" --query-audio \"$AUDIO_FILE\""
            python3 $VISUALIZATION_SCRIPT --query "$QUERY_FILE" --query-audio "$AUDIO_FILE"
            RESULT=$?
            echo "测试命令已执行，退出码: $RESULT"
            
            if [ $RESULT -ne 0 ]; then
                echo "测试失败，尝试使用绝对路径运行..."
                ABSOLUTE_AUDIO=$(realpath "$AUDIO_FILE")
                echo "绝对路径: $ABSOLUTE_AUDIO"
                python3 $VISUALIZATION_SCRIPT --query "$QUERY_FILE" --query-audio "$ABSOLUTE_AUDIO"
                echo "绝对路径测试命令已执行，退出码: $?"
            fi
        else
            echo "测试文件不存在:"
            [ ! -f "$QUERY_FILE" ] && echo "- 查询文件不存在: $QUERY_FILE"
            [ ! -f "$AUDIO_FILE" ] && echo "- 音频文件不存在: $AUDIO_FILE"
            show_menu
        fi
    else
        show_menu
    fi
else
    # Non-interactive mode, just generate static images
    generate_static_images
fi

# 检查matplotlib是否能正常工作和显示图形
echo "测试matplotlib显示图形..."
python3 -c "
import matplotlib.pyplot as plt
import numpy as np
# 创建一个简单的测试图
plt.figure()
x = np.linspace(0, 10, 100)
plt.plot(x, np.sin(x))
plt.title('Matplotlib测试图形')
plt.show(block=False)
print('如果你能看到一个正弦波图形，则matplotlib工作正常')
plt.pause(2)
" || echo "matplotlib图形显示测试失败，这可能是可视化失败的原因"

echo "完成! 如需查看交互式可视化，请直接运行脚本: ./run_visualizations.sh" 