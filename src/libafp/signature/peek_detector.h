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

    void filterPeaks(std::vector<Peak>& peaks, int maxPeaksPerFrame, uint32_t channel, const PeakDetectionConfig& peakConfig);

    // 生成基于对数尺度的频段边界
    std::vector<std::pair<float, float>> generateLogFrequencyBands(float minFreq, float maxFreq, size_t numBands);
    
    // 在指定频段内检测峰值
    std::vector<Peak> extractPeaksInFrequencyBand(
        const std::vector<FFTResult>& fftResults,
        int startIdx, int endIdx,
        float bandMinFreq, float bandMaxFreq,
        float quantileMagnitude,
        const PeakDetectionConfig& peakConfig,
        size_t fftSize);
    
    // 计算频段优先级权重
    std::vector<float> calculateBandPriorityWeights(const std::vector<std::pair<float, float>>& frequencyBands);

    // 动态峰值分配相关方法
    int calculateDynamicPeakCount(
        const std::vector<FFTResult>& fftResults,
        int wndStartIdx, int wndEndIdx,
        const std::vector<std::pair<float, float>>& frequencyBands,
        uint32_t channel);
    
    // 计算频段能量
    std::vector<float> calculateBandEnergies(
        const std::vector<FFTResult>& fftResults,
        int wndStartIdx, int wndEndIdx,
        const std::vector<std::pair<float, float>>& frequencyBands,
        size_t fftSize);
    
    // 估计频段噪声水平
    std::vector<float> estimateBandNoiseLevel(
        const std::vector<FFTResult>& fftResults,
        int wndStartIdx, int wndEndIdx,
        const std::vector<std::pair<float, float>>& frequencyBands,
        uint32_t channel,
        size_t fftSize);
    
    // 计算频段信噪比
    std::vector<float> calculateBandSNR(
        const std::vector<float>& bandEnergies,
        const std::vector<float>& bandNoiseLevel);
    
    // 更新噪声历史记录
    void updateNoiseHistory(
        uint32_t channel,
        const std::vector<float>& bandNoiseLevel,
        double timestamp);

private:
    std::shared_ptr<IPerformanceConfig> config_;
    std::map<uint32_t, std::vector<Peak>> peakCache_;
    
    // 每个通道的峰值检测滑动窗口信息
    std::map<uint32_t, SlidingWindowInfo> slidingWindowMap_;

    bool* collectVisualizationData_;
    VisualizationData* visualizationData_;
    
    // 噪声估计历史记录
    struct NoiseHistoryEntry {
        double timestamp;
        std::vector<float> bandNoiseLevel;
    };
    std::map<uint32_t, std::vector<NoiseHistoryEntry>> noiseHistory_;
};
}