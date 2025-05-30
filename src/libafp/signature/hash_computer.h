#pragma once

#include "signature_generator.h"
#include <vector>
#include <deque>

namespace afp {

// 双帧峰值组合评分结构
struct ScoredPeakCombination {
    const Peak* anchorPeak;
    const Peak* targetPeak;
    double score;
    uint32_t hash;
    
    // 用于排序的比较函数
    bool operator>(const ScoredPeakCombination& other) const {
        return score > other.score;
    }
};

// 三帧峰值组合评分结构
struct ScoredTripleFrameCombination {
    const Peak* anchorPeak;
    const Peak* targetPeak1;
    const Peak* targetPeak2;
    double score;
    uint32_t hash;
    
    // 用于排序的比较函数
    bool operator>(const ScoredTripleFrameCombination& other) const {
        return score > other.score;
    }
};

// 哈希计算器
class HashComputer {
public:
    HashComputer(std::shared_ptr<IPerformanceConfig> config, bool* collectVisualizationData, VisualizationData* visualizationData);
    ~HashComputer();

    struct ComputeHashReturn {
        bool isHashComputed;
        size_t consumedFrameCount;  // 已消费的帧数量，用于指导帧移除
    };

    // 计算哈希值
    ComputeHashReturn computeHash(const std::deque<Frame>& longFrames, std::vector<SignaturePoint>& signatures);

    ComputeHashReturn computeHash2(
        const std::deque<Frame>& longFrames, 
        std::vector<SignaturePoint>& signatures);

private:
    // 计算三帧组合哈希值
    uint32_t computeTripleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

    uint32_t computeDoubleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak);
    
    // 计算双帧峰值组合的评分
    double computeDoubleFrameCombinationScore(
        const Peak& anchorPeak,
        const Peak& targetPeak);

    // 计算三帧峰值组合的评分
    double computeTripleFrameCombinationScore(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

private:
    std::shared_ptr<IPerformanceConfig> config_;

    bool* collectVisualizationData_;
    VisualizationData* visualizationData_;
};
}