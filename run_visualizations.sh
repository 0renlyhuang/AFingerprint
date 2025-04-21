#!/bin/bash

# This script finds and runs all visualization Python scripts

# Find all Python visualization scripts
SCRIPTS=$(find . -name "*fingerprint.json.py" -o -name "comparison_*.py")

# Check if any scripts were found
if [ -z "$SCRIPTS" ]; then
    echo "No visualization scripts found."
    echo "Try looking in the build directory..."
    
    # 尝试在build目录中查找
    BUILD_SCRIPTS=$(find ./build -name "*fingerprint.json.py" -o -name "comparison_*.py")
    
    if [ -z "$BUILD_SCRIPTS" ]; then
        echo "Still no visualization scripts found."
        echo "Run the program with --visualize flag first."
        exit 1
    else
        SCRIPTS=$BUILD_SCRIPTS
        echo "Found scripts in build directory."
    fi
fi

# Make sure matplotlib is installed
python3 -c "import matplotlib" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Installing matplotlib..."
    pip3 install matplotlib
fi

# Make sure os module is available (should be part of standard library)
python3 -c "import os" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "Error: Python os module not available."
    exit 1
fi

# Run each script
echo "Running visualization scripts..."
for script in $SCRIPTS; do
    echo "  Running $script..."
    # 获取脚本所在目录
    SCRIPT_DIR=$(dirname "$script")
    # 切换到脚本所在目录再执行，确保相对路径正确
    (cd "$SCRIPT_DIR" && python3 "$(basename "$script")")
done

echo "Done! All visualizations have been generated."
echo "Check the corresponding directories for PNG files." 