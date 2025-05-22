#pragma once

#include "signature_generator.h"
#include <vector>
#include <map>

namespace afp {

// 峰值检测器
class PeekDetector {
public:
    PeekDetector(std::shared_ptr<IPerformanceConfig> config, bool* collectVisualizationData, VisualizationData* visualizationData);
    ~PeekDetector();

    struct recvFFTResultReturn {
        bool isPeekDetectionSatisfied;
        double lastConfirmTime;
        int fftConsumedCount;
    };

    // 接收FFT结果
    recvFFTResultReturn recvFFTResult(uint32_t channel, const std::vector<FFTResult>& fftResult, double currentTimestamp);
    
    // 获取峰值缓存
    const std::vector<Peak>& getPeakCache(uint32_t channel) const;
    
    // 擦除特定时间戳之前的峰值缓存
    void erasePeakCache(uint32_t channel, double consumedTimestamp);
    
    // 重置所有数据
    void reset();

private:
    // 从短帧FFT结果中提取峰值
    std::vector<Peak> extractPeaksFromFFTResults(
        const std::vector<FFTResult>& fftResults,
        int wndStartIdx,
        int wndEndIdx,
        double windowStartTime,
        double windowEndTime);
        
    // 基于滑动窗口检测峰值
    void detectPeaksInSlidingWindow(
        uint32_t channel, 
        const std::vector<FFTResult>& fftResults, 
        int wndStartIdx, int wndEndIdx, 
        double windowStartTime, 
        double windowEndTime);

private:
    std::shared_ptr<IPerformanceConfig> config_;
    std::map<uint32_t, std::vector<Peak>> peakCache_;
    
    // 每个通道的峰值检测滑动窗口信息
    std::map<uint32_t, SlidingWindowInfo> slidingWindowMap_;

    bool* collectVisualizationData_;
    VisualizationData* visualizationData_;
};
}