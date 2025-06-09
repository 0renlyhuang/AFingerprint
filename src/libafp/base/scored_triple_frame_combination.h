#pragma once

#include "base/peek.h"

namespace afp {

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

}