#include "debugger/visualization.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <unordered_map>
#include <vector>

namespace afp {

Visualizer::Visualizer() {}

Visualizer::~Visualizer() {}

// Singleton instance
Visualizer& Visualizer::getInstance() {
    static Visualizer instance;
    return instance;
}

bool Visualizer::saveVisualization(const VisualizationData& data, const std::string& filename) {
    // Create and open a JSON file to save the data
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    // Write JSON header
    file << "{\n";
    file << "  \"title\": \"" << data.title << "\",\n";
    file << "  \"duration\": " << data.duration << ",\n";
    
    // Write peaks
    file << "  \"allPeaks\": [\n";
    for (size_t i = 0; i < data.allPeaks.size(); ++i) {
        file << "    [" << data.allPeaks[i].first << ", " << data.allPeaks[i].second << "]";
        if (i < data.allPeaks.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    
    // Write fingerprint points
    file << "  \"fingerprintPoints\": [\n";
    for (size_t i = 0; i < data.fingerprintPoints.size(); ++i) {
        file << "    [" << std::get<0>(data.fingerprintPoints[i]) << ", " 
             << std::get<1>(data.fingerprintPoints[i]) << ", "
             << "\"0x" << std::hex << std::get<2>(data.fingerprintPoints[i]) << std::dec << "\"]";
        if (i < data.fingerprintPoints.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    
    // Write matched points
    file << "  \"matchedPoints\": [\n";
    for (size_t i = 0; i < data.matchedPoints.size(); ++i) {
        file << "    [" << std::get<0>(data.matchedPoints[i]) << ", " 
             << std::get<1>(data.matchedPoints[i]) << ", "
             << "\"0x" << std::hex << std::get<2>(data.matchedPoints[i]) << std::dec << "\"]";
        if (i < data.matchedPoints.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ]\n";
    
    // Write JSON footer
    file << "}\n";
    
    file.close();
    std::cout << "Visualization data saved to: " << filename << std::endl;
    
    return true;
}

bool Visualizer::generateExtractionPlot(const VisualizationData& data, const std::string& filename) {
    // Generate Python script to plot the data
    std::ofstream pyScript(filename + ".py");
    if (!pyScript.is_open()) {
        std::cerr << "Failed to create Python script: " << filename << ".py" << std::endl;
        return false;
    }
    
    pyScript << "import matplotlib.pyplot as plt\n";
    pyScript << "import json\n";
    pyScript << "import numpy as np\n";
    pyScript << "import os\n\n";
    
    pyScript << "# Get current directory\n";
    pyScript << "current_dir = os.path.dirname(os.path.abspath(__file__))\n";
    pyScript << "# Remove .py extension from this script path to get the JSON file path\n";
    pyScript << "json_file = os.path.splitext(__file__)[0]\n\n";
    
    pyScript << "# Load data from JSON file\n";
    pyScript << "with open(json_file, 'r') as f:\n";
    pyScript << "    data = json.load(f)\n\n";
    
    pyScript << "# Create plot\n";
    pyScript << "plt.figure(figsize=(15, 8))\n";
    
    // Plot all peaks
    pyScript << "# Plot all detected peaks\n";
    pyScript << "peak_freqs = [peak[0] for peak in data['allPeaks']]\n";
    pyScript << "peak_times = [peak[1] for peak in data['allPeaks']]\n";
    pyScript << "plt.scatter(peak_times, peak_freqs, color='blue', alpha=0.3, s=10, label='All Peaks')\n\n";
    
    // Plot fingerprint points
    pyScript << "# Plot selected fingerprint points\n";
    pyScript << "fp_freqs = [point[0] for point in data['fingerprintPoints']]\n";
    pyScript << "fp_times = [point[1] for point in data['fingerprintPoints']]\n";
    pyScript << "plt.scatter(fp_times, fp_freqs, color='red', s=25, label='Fingerprint Points')\n\n";
    
    // Set title and labels
    pyScript << "plt.title(f\"Audio Fingerprint Extraction: {data['title']}\")\n";
    pyScript << "plt.xlabel('Time (s)')\n";
    pyScript << "plt.ylabel('Frequency (Hz)')\n";
    pyScript << "plt.ylim(0, 5000)  # Limit frequency display range\n";
    pyScript << "plt.grid(True, alpha=0.3)\n";
    pyScript << "plt.legend()\n\n";
    
    // Save the plot
    pyScript << "output_png = json_file + '.png'\n";
    pyScript << "plt.savefig(output_png)\n";
    pyScript << "print(f\"Plot saved to: {output_png}\")\n";
    
    pyScript.close();
    
    std::cout << "Generated Python plotting script: " << filename << ".py" << std::endl;
    std::cout << "To generate the plot, run: python " << filename << ".py" << std::endl;
    
    return true;
}

bool Visualizer::generateMatchingPlot(const VisualizationData& sourceData, 
                                     const VisualizationData& queryData,
                                     const std::string& filename) {
    // Save both datasets to JSON
    std::string sourceFile = filename + "_source.json";
    std::string queryFile = filename + "_query.json";
    
    // 为了在source中也显示匹配点，需要创建一个临时的sourceData副本
    VisualizationData enhancedSourceData = sourceData;
    
    // 将查询数据中的匹配点也添加到源数据的匹配点列表中
    // 需要根据哈希值找到源音频中对应的指纹点
    if (!queryData.matchedPoints.empty()) {
        // 创建源音频哈希到索引的映射，便于快速查找
        std::unordered_map<uint32_t, std::vector<size_t>> sourceHashToIndex;
        for (size_t i = 0; i < sourceData.fingerprintPoints.size(); ++i) {
            const auto& point = sourceData.fingerprintPoints[i];
            uint32_t hash = std::get<2>(point);
            sourceHashToIndex[hash].push_back(i);
        }
        
        // 将查询数据中的匹配点对应的哈希在源数据中找到，并添加到源数据的匹配点中
        for (const auto& matchPoint : queryData.matchedPoints) {
            uint32_t hash = std::get<2>(matchPoint);
            
            // 在源数据中查找相同哈希值的点
            auto it = sourceHashToIndex.find(hash);
            if (it != sourceHashToIndex.end()) {
                for (size_t idx : it->second) {
                    // 添加到源数据的匹配点列表
                    enhancedSourceData.matchedPoints.push_back(sourceData.fingerprintPoints[idx]);
                }
            }
        }
    }
    
    if (!saveVisualization(enhancedSourceData, sourceFile) || 
        !saveVisualization(queryData, queryFile)) {
        return false;
    }
    
    // Generate Python script to plot the comparison
    std::ofstream pyScript(filename + ".py");
    if (!pyScript.is_open()) {
        std::cerr << "Failed to create Python script: " << filename << ".py" << std::endl;
        return false;
    }
    
    pyScript << "import matplotlib.pyplot as plt\n";
    pyScript << "import json\n";
    pyScript << "import numpy as np\n";
    pyScript << "import os\n\n";
    
    pyScript << "# Get current directory and script base name\n";
    pyScript << "current_dir = os.path.dirname(os.path.abspath(__file__))\n";
    pyScript << "base_name = os.path.splitext(os.path.basename(__file__))[0]\n\n";
    
    pyScript << "# Construct paths to source and query JSON files\n";
    pyScript << "source_file = os.path.join(current_dir, base_name + '_source.json')\n";
    pyScript << "query_file = os.path.join(current_dir, base_name + '_query.json')\n\n";
    
    pyScript << "# Load source data\n";
    pyScript << "with open(source_file, 'r') as f:\n";
    pyScript << "    source_data = json.load(f)\n\n";
    
    pyScript << "# Load query data\n";
    pyScript << "with open(query_file, 'r') as f:\n";
    pyScript << "    query_data = json.load(f)\n\n";
    
    pyScript << "# Create figure with two subplots\n";
    pyScript << "fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(15, 12), sharex=True)\n\n";
    
    // First subplot - Source Audio
    pyScript << "# Plot source audio (top)\n";
    pyScript << "ax1.scatter([peak[1] for peak in source_data['allPeaks']], \n";
    pyScript << "            [peak[0] for peak in source_data['allPeaks']], \n";
    pyScript << "            color='blue', alpha=0.3, s=10, label='Source Peaks')\n\n";
    
    pyScript << "ax1.scatter([point[1] for point in source_data['fingerprintPoints']], \n";
    pyScript << "            [point[0] for point in source_data['fingerprintPoints']], \n";
    pyScript << "            color='red', s=25, label='Source Fingerprints')\n\n";
    
    // 添加在源音频中显示匹配点的代码
    pyScript << "# Highlight matched points in source audio\n";
    pyScript << "if source_data['matchedPoints']:\n";
    pyScript << "    ax1.scatter([point[1] for point in source_data['matchedPoints']], \n";
    pyScript << "                [point[0] for point in source_data['matchedPoints']], \n";
    pyScript << "                color='orange', s=100, alpha=0.8, marker='*', label='Matched Points')\n\n";
    
    pyScript << "ax1.set_title(f\"Source Audio: {source_data['title']}\")\n";
    pyScript << "ax1.set_ylabel('Frequency (Hz)')\n";
    pyScript << "ax1.set_ylim(0, 5000)\n";
    pyScript << "ax1.grid(True, alpha=0.3)\n";
    pyScript << "ax1.legend()\n\n";
    
    // Second subplot - Query Audio
    pyScript << "# Plot query audio (bottom)\n";
    pyScript << "ax2.scatter([peak[1] for peak in query_data['allPeaks']], \n";
    pyScript << "            [peak[0] for peak in query_data['allPeaks']], \n";
    pyScript << "            color='green', alpha=0.3, s=10, label='Query Peaks')\n\n";
    
    pyScript << "ax2.scatter([point[1] for point in query_data['fingerprintPoints']], \n";
    pyScript << "            [point[0] for point in query_data['fingerprintPoints']], \n";
    pyScript << "            color='purple', s=25, label='Query Fingerprints')\n\n";
    
    pyScript << "# Highlight matched points in query audio\n";
    pyScript << "if query_data['matchedPoints']:\n";
    pyScript << "    ax2.scatter([point[1] for point in query_data['matchedPoints']], \n";
    pyScript << "                [point[0] for point in query_data['matchedPoints']], \n";
    pyScript << "                color='orange', s=100, alpha=0.8, marker='*', label='Matched Points')\n\n";
    
    pyScript << "ax2.set_title(f\"Query Audio: {query_data['title']} (with matches)\")\n";
    pyScript << "ax2.set_xlabel('Time (s)')\n";
    pyScript << "ax2.set_ylabel('Frequency (Hz)')\n";
    pyScript << "ax2.set_ylim(0, 5000)\n";
    pyScript << "ax2.grid(True, alpha=0.3)\n";
    pyScript << "ax2.legend()\n\n";
    
    // 添加连接匹配点的线条
    pyScript << "# Draw lines connecting all matching points across plots\n";
    pyScript << "if source_data['matchedPoints'] and query_data['matchedPoints']:\n";
    pyScript << "    # Import ConnectionPatch for drawing lines between subplots\n";
    pyScript << "    from matplotlib.patches import ConnectionPatch\n";
    pyScript << "    \n";
    pyScript << "    # Create dictionaries to group points by hash values\n";
    pyScript << "    source_points_by_hash = {}\n";
    pyScript << "    query_points_by_hash = {}\n";
    pyScript << "    \n";
    pyScript << "    # Extract hash from the third element (index 2) and convert it to int if it's a string like '0x...'\n";
    pyScript << "    for point in source_data['matchedPoints']:\n";
    pyScript << "        hash_value = point[2]\n";
    pyScript << "        if isinstance(hash_value, str) and hash_value.startswith('0x'):\n";
    pyScript << "            hash_value = int(hash_value, 16)\n";
    pyScript << "        if hash_value not in source_points_by_hash:\n";
    pyScript << "            source_points_by_hash[hash_value] = []\n";
    pyScript << "        source_points_by_hash[hash_value].append((point[1], point[0]))  # (time, freq)\n";
    pyScript << "    \n";
    pyScript << "    for point in query_data['matchedPoints']:\n";
    pyScript << "        hash_value = point[2]\n";
    pyScript << "        if isinstance(hash_value, str) and hash_value.startswith('0x'):\n";
    pyScript << "            hash_value = int(hash_value, 16)\n";
    pyScript << "        if hash_value not in query_points_by_hash:\n";
    pyScript << "            query_points_by_hash[hash_value] = []\n";
    pyScript << "        query_points_by_hash[hash_value].append((point[1], point[0]))  # (time, freq)\n";
    pyScript << "    \n";
    pyScript << "    # Connect points with the same hash values\n";
    pyScript << "    for hash_value, source_points in source_points_by_hash.items():\n";
    pyScript << "        if hash_value in query_points_by_hash:\n";
    pyScript << "            query_points = query_points_by_hash[hash_value]\n";
    pyScript << "            # Connect each source point to each query point with the same hash\n";
    pyScript << "            for source_point in source_points:\n";
    pyScript << "                for query_point in query_points:\n";
    pyScript << "                    con = ConnectionPatch(xyA=source_point, xyB=query_point,\n";
    pyScript << "                                       coordsA='data', coordsB='data',\n";
    pyScript << "                                       axesA=ax1, axesB=ax2, color='red', alpha=0.2, linestyle='--')\n";
    pyScript << "                    fig.add_artist(con)\n";
    
    // Save the plot
    pyScript << "plt.tight_layout()\n";
    pyScript << "output_png = os.path.join(current_dir, base_name + '.png')\n";
    pyScript << "plt.savefig(output_png)\n";
    pyScript << "print(f\"Comparison plot saved to: {output_png}\")\n";
    
    pyScript.close();
    
    std::cout << "Generated comparison Python plotting script: " << filename << ".py" << std::endl;
    std::cout << "To generate the plot, run: python " << filename << ".py" << std::endl;
    
    return true;
}

} // namespace afp 