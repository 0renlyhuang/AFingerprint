#include "long_frame_builder.h"
#include <iostream>
#include <algorithm>

namespace afp {

LongFrameBuilder::LongFrameBuilder(std::shared_ptr<IPerformanceConfig> config)
    : config_(config) {
}

LongFrameBuilder::~LongFrameBuilder() = default;

// 构建长帧
LongFrameBuilder::BuildLongFrameReturn LongFrameBuilder::buildLongFrame(
    uint32_t channel, 
    const std::vector<Peak>& peaks, 
    double currentTimestamp) {
    
    BuildLongFrameReturn result;
    result.isLongFrameBuilt = false;
    result.longFrameTimestamp = 0.0;
    
    if (peaks.empty()) {
        return result;
    }
    
    // 获取长帧配置
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    const double frameDuration = signatureConfig.frameDuration;
    
    // 初始化当前通道的滑动窗口信息（如果不存在）
    if (slidingWindowMap_.find(channel) == slidingWindowMap_.end()) {
        SlidingWindowInfo windowInfo;
        double firstTimestamp = peaks.front().timestamp;
        windowInfo.currentWindowStart = firstTimestamp;
        windowInfo.currentWindowEnd = firstTimestamp + frameDuration;
        windowInfo.nextWindowStartTime = firstTimestamp + frameDuration;
        windowInfo.windowReady = false;
        slidingWindowMap_[channel] = windowInfo;
        
        std::cout << "[DEBUG-长帧] LongFrameBuilder: 通道" << channel 
                  << "初始化长帧窗口, 起始时间: " << firstTimestamp 
                  << "s, 结束时间: " << (firstTimestamp + frameDuration) << "s" << std::endl;
    }
    
    // 获取当前通道的滑动窗口信息
    SlidingWindowInfo& windowInfo = slidingWindowMap_[channel];
    
    // 记录是否构建了任何长帧
    bool anyFrameBuilt = false;
    double lastProcessedWindowEnd = 0.0; // 最后处理的窗口结束时间
    
    // 如果最新的确认时间戳超过了当前窗口的结束时间，可以构建长帧
    // 使用循环来处理可能有多个窗口需要处理的情况
    while (currentTimestamp >= windowInfo.currentWindowEnd) {
        windowInfo.windowReady = true;
        
        std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                  << "检测到currentTimestamp(" << currentTimestamp 
                  << "s) >= windowEnd(" << windowInfo.currentWindowEnd 
                  << "s), 尝试构建长帧" << std::endl;
        
        // 记录当前窗口结束时间，用于追踪已消费的峰值
        double currentWindowEnd = windowInfo.currentWindowEnd;
        
        // 构建长帧
        bool frameBuilt = processLongFrame(channel, peaks);
        
        if (frameBuilt) {
            anyFrameBuilt = true;
            // 保存当前窗口的结束时间而不是开始时间，确保所有小于此时间的峰值都被标记为已消费
            lastProcessedWindowEnd = currentWindowEnd;
            
            std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                      << "成功构建了窗口" << windowInfo.currentWindowStart 
                      << "s - " << currentWindowEnd 
                      << "s的长帧，更新已消费时间戳为" << lastProcessedWindowEnd << "s" << std::endl;
        } else {
            std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                      << "未能构建窗口" << windowInfo.currentWindowStart 
                      << "s - " << currentWindowEnd << "s的长帧" << std::endl;
        }
        
        // 显式滑动窗口到下一个位置，无论是否成功构建长帧
        slideWindow(channel);
        
        std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                  << "滑动窗口到" << windowInfo.currentWindowStart
                  << "s - " << windowInfo.currentWindowEnd << "s" << std::endl;
        
        // 如果我们已经滑动窗口，但当前时间戳小于新窗口的结束时间，
        // 则已经处理完所有需要处理的窗口，退出循环
        if (currentTimestamp < windowInfo.currentWindowEnd) {
            std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                      << "已处理完所有窗口，当前时间戳" << currentTimestamp 
                      << "s < 下一窗口结束时间" << windowInfo.currentWindowEnd 
                      << "s, 退出循环" << std::endl;
            break;
        }
        
        // 检查是否仍有足够的峰值用于下一个窗口
        if (peaks.empty()) {
            std::cout << "[DEBUG-长帧循环] LongFrameBuilder: 通道" << channel 
                      << "峰值缓存为空，无法继续构建长帧，退出循环" << std::endl;
            break;
        }
    }
    
    // 设置结果
    if (anyFrameBuilt) {
        result.isLongFrameBuilt = true;
        // 使用最后处理的窗口结束时间作为已消费时间戳，而不是窗口开始时间
        result.longFrameTimestamp = lastProcessedWindowEnd;
        
        std::cout << "[DEBUG-长帧结果] LongFrameBuilder: 通道" << channel 
                  << "构建了至少一个长帧，最后消费的时间戳: " 
                  << result.longFrameTimestamp << "s" << std::endl;
    }
    
    return result;
}

// 处理长帧
bool LongFrameBuilder::processLongFrame(uint32_t channel, const std::vector<Peak>& allPeaks) {
    // 获取当前通道的滑动窗口信息
    if (slidingWindowMap_.find(channel) == slidingWindowMap_.end()) {
        std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel << "没有长帧滑动窗口信息" << std::endl;
        return false;
    }
    
    SlidingWindowInfo& windowInfo = slidingWindowMap_[channel];
    double frameStartTime = windowInfo.currentWindowStart;
    double frameEndTime = windowInfo.currentWindowEnd;
    
    std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel << "开始处理长帧，时间范围: " << frameStartTime 
              << "s - " << frameEndTime << "s, 当前峰值缓存大小: " << allPeaks.size() << std::endl;
    
    // 从峰值缓存中收集位于当前长帧时间范围内的峰值
    std::vector<Peak> framePeaks;
    
    // 输出详细的峰值时间戳信息，以便调试
    std::cout << "[DEBUG-详细] LongFrameBuilder: 通道" << channel << "检查时间范围" << frameStartTime 
              << "s - " << frameEndTime << "s的峰值:" << std::endl;
    std::cout << "[DEBUG-详细] 峰值缓存中共有" << allPeaks.size() << "个峰值，时间戳: ";
    
    // 输出所有峰值的时间戳
    for (const auto& peak : allPeaks) {
        std::cout << peak.timestamp << "s(" << peak.frequency << "Hz) ";
        
        // 检查是否在当前长帧时间范围内
        if (peak.timestamp >= frameStartTime && peak.timestamp < frameEndTime) {
            framePeaks.push_back(peak);
            std::cout << "✓ ";  // 标记包含在当前帧内的峰值
        } else {
            std::cout << "✗ ";  // 标记不在当前帧内的峰值
        }
    }
    std::cout << std::endl;
    
    // 如果当前长帧时间范围内没有足够的峰值，放弃创建长帧
    if (framePeaks.empty()) {
        std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel << "在时间范围" << frameStartTime 
                  << "s - " << frameEndTime << "s内没有峰值，放弃创建长帧" << std::endl;
        return false;
    }
    
    std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel << "在时间范围" << frameStartTime 
              << "s - " << frameEndTime << "s内找到" << framePeaks.size() << "个峰值" << std::endl;
    
    // 创建新帧并存储其峰值
    Frame newFrame;
    newFrame.peaks = framePeaks;
    newFrame.timestamp = frameStartTime; // 保留长帧开始时间作为帧时间戳

    std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel << "创建了新的长帧，峰值数量: " 
              << framePeaks.size() << ", 历史长帧数量: " << frameMap_[channel].size() << std::endl;

    // 为当前通道添加帧历史记录
    frameMap_[channel].push_back(newFrame);
    
    return true;
}

// 移除长帧
void LongFrameBuilder::removeConsumedLongFrame(uint32_t channel) {
    // 只保留最新的2帧
    std::vector<double> removedTimestamps;
    while (frameMap_.find(channel) != frameMap_.end() && frameMap_[channel].size() > 2) {
        double removedTimestamp = frameMap_[channel].front().timestamp;
        removedTimestamps.push_back(removedTimestamp);
        frameMap_[channel].pop_front();
    }

    if (!removedTimestamps.empty()) {
        std::ostringstream timestampsStr;
        for (size_t i = 0; i < removedTimestamps.size(); ++i) {
            if (i > 0) timestampsStr << ", ";
            timestampsStr << removedTimestamps[i] << "s";
        }
        
        std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel 
                  << "移除了长帧，时间戳: [" << timestampsStr.str() 
                  << "], 当前长帧数量: " << frameMap_[channel].size() << std::endl;
    } else {
        std::cout << "[DEBUG-长帧处理] LongFrameBuilder: 通道" << channel 
                  << "没有移除长帧，当前长帧数量: " << frameMap_[channel].size() << std::endl;
    }
}

// 滑动窗口
void LongFrameBuilder::slideWindow(uint32_t channel) {
    if (slidingWindowMap_.find(channel) == slidingWindowMap_.end()) {
        return;
    }
    
    SlidingWindowInfo& windowInfo = slidingWindowMap_[channel];
    
    // 获取帧配置
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    
    // 更新窗口到下一个位置
    windowInfo.lastProcessedTime = windowInfo.currentWindowEnd;
    windowInfo.currentWindowStart = windowInfo.nextWindowStartTime;
    windowInfo.currentWindowEnd = windowInfo.currentWindowStart + signatureConfig.frameDuration;
    windowInfo.nextWindowStartTime = windowInfo.currentWindowStart + signatureConfig.frameDuration;
    windowInfo.windowReady = false;
    
    std::cout << "[DEBUG-长帧] LongFrameBuilder: 通道" << channel << "长帧窗口滑动到" 
              << windowInfo.currentWindowStart << "s-" << windowInfo.currentWindowEnd 
              << "s" << std::endl;
}

// 获取长帧历史
const std::deque<Frame>& LongFrameBuilder::getLongFrames(uint32_t channel) const {
    static const std::deque<Frame> emptyFrames;
    if (frameMap_.find(channel) != frameMap_.end()) {
        return frameMap_.at(channel);
    }
    return emptyFrames;
}

// 重置所有数据
void LongFrameBuilder::reset() {
    frameMap_.clear();
    slidingWindowMap_.clear();
    std::cout << "[DEBUG-重置] LongFrameBuilder: 已重置所有长帧历史和滑动窗口状态" << std::endl;
}

} // namespace afp
