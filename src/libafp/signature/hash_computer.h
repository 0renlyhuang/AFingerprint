#pragma once

#include "signature_generator.h"
#include <vector>
#include <deque>

namespace afp {

// 哈希计算器
class HashComputer {
public:
    HashComputer(std::shared_ptr<IPerformanceConfig> config, bool* collectVisualizationData, VisualizationData* visualizationData);
    ~HashComputer();

    struct ComputeHashReturn {
        bool isHashComputed;
    };

    // 计算哈希值
    ComputeHashReturn computeHash(const std::deque<Frame>& longFrames, std::vector<SignaturePoint>& signatures);

private:
    // 计算三帧组合哈希值
    uint32_t computeTripleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

private:
    std::shared_ptr<IPerformanceConfig> config_;

    bool* collectVisualizationData_;
    VisualizationData* visualizationData_;
};
}