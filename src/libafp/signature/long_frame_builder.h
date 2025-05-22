#pragma once

#include "signature_generator.h"
#include <iostream>
#include <deque>
#include <map>
#include <vector>
#include <memory>
#include <sstream>

namespace afp {

// 长帧构建器
class LongFrameBuilder {
public:
    LongFrameBuilder(std::shared_ptr<IPerformanceConfig> config_);
    ~LongFrameBuilder();

    struct BuildLongFrameReturn {
        bool isLongFrameBuilt;
        double longFrameTimestamp;
    };

    // 构建长帧
    BuildLongFrameReturn buildLongFrame(uint32_t channel, const std::vector<Peak>& peaks, double currentTimestamp);
    
    // 获取长帧历史
    const std::deque<Frame>& getLongFrames(uint32_t channel) const;

    // 移除长帧
    void removeConsumedLongFrame(uint32_t channel);
    
    // 重置所有数据
    void reset();

private:
    // 处理长帧
    bool processLongFrame(uint32_t channel, const std::vector<Peak>& allPeaks);
    
    // 滑动窗口
    void slideWindow(uint32_t channel);

private:
    std::shared_ptr<IPerformanceConfig> config_;
    
    // 每个通道的长帧历史
    std::map<uint32_t, std::deque<Frame>> frameMap_;
    
    // 每个通道的长帧滑动窗口信息
    std::map<uint32_t, SlidingWindowInfo> slidingWindowMap_;
};
}