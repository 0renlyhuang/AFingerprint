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

# Make sure Python visualization script exists
VISUALIZATION_SCRIPT="src/visualization/visualize_fingerprints.py"
if [ ! -f "$VISUALIZATION_SCRIPT" ]; then
    echo "Error: Visualization script not found at $VISUALIZATION_SCRIPT"
    exit 1
fi

# Make sure script is executable
chmod +x $VISUALIZATION_SCRIPT

# Locate all JSON fingerprint files
EXTRACTION_FILES=$(find . -name "*_fingerprint.json")
QUERY_FILES=$(find . -name "*_query.json")
SOURCE_FILES=$(find . -name "*_source.json")
SESSION_FILES=$(find . -name "*_sessions.json")

echo "Found:"
echo "  - Extraction data files: $(echo $EXTRACTION_FILES | wc -w)"
echo "  - Query data files: $(echo $QUERY_FILES | wc -w)"
echo "  - Source data files: $(echo $SOURCE_FILES | wc -w)"
echo "  - Session data files: $(echo $SESSION_FILES | wc -w)"

# Function to choose between interactive and static mode
function show_menu() {
    echo ""
    echo "选择可视化模式："
    echo "1. 生成静态图像 (生成 PNG 文件)"
    echo "2. 启动交互式可视化 (支持悬停查看详情)"
    echo ""
    read -p "请选择 (1/2): " mode_choice
    
    if [ "$mode_choice" == "1" ]; then
        generate_static_images
    elif [ "$mode_choice" == "2" ]; then
        launch_interactive_visualization
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

# Function to launch interactive visualization
function launch_interactive_visualization() {
    echo "启动交互式可视化..."
    
    # Gather all visualization combinations
    visualizations=()
    
    # Add all extraction visualizations
    for file in $EXTRACTION_FILES; do
        visualizations+=("提取可视化: $(basename $file) - python3 $VISUALIZATION_SCRIPT --source \"$file\"")
    done
    
    # Add all query visualizations
    for file in $QUERY_FILES; do
        visualizations+=("查询可视化: $(basename $file) - python3 $VISUALIZATION_SCRIPT --query \"$file\"")
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
            # Extract and run the command
            cmd=$(echo "${visualizations[$((viz_choice-1))]}" | sed 's/.*- \(python3.*\)/\1/')
            echo "运行: $cmd"
            eval "$cmd"
        else
            echo "无效选择，请重试"
        fi
    done
}

# Main script execution
if [ -t 0 ]; then  # Check if script is running interactively
    show_menu
else
    # Non-interactive mode, just generate static images
    generate_static_images
fi

echo "完成! 如需查看交互式可视化，请直接运行脚本: ./run_visualizations.sh" 