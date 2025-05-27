#include "peek_detector.h"
#include <iostream>
#include <algorithm>
#include <cmath>

namespace afp {

PeekDetector::PeekDetector(std::shared_ptr<IPerformanceConfig> config, bool* collectVisualizationData, VisualizationData* visualizationData)
    : config_(config), collectVisualizationData_(collectVisualizationData), visualizationData_(visualizationData) {
}

PeekDetector::~PeekDetector() = default;

// 接收FFT结果，检测峰值
PeekDetector::recvFFTResultReturn PeekDetector::recvFFTResult(
    uint32_t channel, 
    const std::vector<FFTResult>& fftResults,
    double currentTimestamp) {
    
    recvFFTResultReturn result;
    result.isPeekDetectionSatisfied = false;
    result.lastConfirmTime = 0.0;
    result.fftConsumedCount = 0;

    // 获取峰值检测配置
    const auto& peakConfig = config_->getPeakDetectionConfig();
    const double peakTimeDuration = peakConfig.peakTimeDuration;
    
    // 初始化当前通道的滑动窗口信息（如果不存在）
    if (slidingWindowMap_.find(channel) == slidingWindowMap_.end()) {
        // 初始化窗口起始时间为第一个短帧时间戳
        if (!fftResults.empty()) {
            SlidingWindowInfo windowInfo;
            double firstTimestamp = fftResults.front().timestamp;
            windowInfo.currentWindowStart = firstTimestamp;
            windowInfo.currentWindowEnd = firstTimestamp + peakTimeDuration;
            windowInfo.nextWindowStartTime = firstTimestamp + peakTimeDuration;
            windowInfo.windowReady = false;
            slidingWindowMap_[channel] = windowInfo;
        }
    }

    int wndStartIdx = 0;  // 大于windowStart的第一个短帧
    int wndEndIdx = 0;  // 小于windowEnd的最后一个短帧
    while (true) {
        // 检查是否有足够的短帧用于峰值检测
        if (fftResults.size() < 2 * peakConfig.timeMaxRange + 1) {
            break; // 短帧数量不足
        }
    
        // 获取当前通道的滑动窗口信息
        SlidingWindowInfo& windowInfo = slidingWindowMap_[channel];
        const auto windowStartTime = windowInfo.currentWindowStart;
        const auto windowEndTime = windowInfo.currentWindowEnd;
        
        if (fftResults[fftResults.size() - 1 - peakConfig.timeMaxRange + 1].timestamp < windowEndTime) {
            break; // 边界余量不足
        }
        // 窗口收集完成
        windowInfo.windowReady = true;

        // 选择在当前窗口时间范围内的短帧
        const auto lastEndIdx = wndEndIdx;
        wndStartIdx = -1;
        wndEndIdx = 0;
        for (size_t i = lastEndIdx; i < fftResults.size(); ++i) {
            if (wndStartIdx == -1 && fftResults[i].timestamp >= windowStartTime) {
                wndStartIdx = i;
            }
            if (wndEndIdx == 0 && fftResults[i].timestamp >= windowEndTime) {
                wndEndIdx = i - 1;
            }
            if (wndStartIdx != -1 && wndEndIdx != 0) {
                break;
            }
        }
        if (wndStartIdx == -1 || wndEndIdx == 0) {
            break;
        }

        std::cout << "[DEBUG-峰值检测] PeekDetector: 通道" << channel << "在窗口" 
            << windowStartTime << "s-" << windowEndTime 
            << "s中检测峰值" << std::endl;
        
        // 检测峰值
        detectPeaksInSlidingWindow(channel, fftResults, wndStartIdx, wndEndIdx, windowStartTime, windowEndTime);
        
        // 设置结果
        result.isPeekDetectionSatisfied = true;
        result.lastConfirmTime = fftResults[wndEndIdx - 1].timestamp;

        // 更新窗口
        windowInfo.currentWindowStart = windowInfo.nextWindowStartTime;
        windowInfo.currentWindowEnd = windowInfo.currentWindowStart + config_->getPeakDetectionConfig().peakTimeDuration;
        windowInfo.nextWindowStartTime = windowInfo.currentWindowStart + config_->getPeakDetectionConfig().peakTimeDuration;
        
        std::cout << "[DEBUG-窗口] PeekDetector: 通道" << channel << "峰值检测窗口滑动到" 
            << windowInfo.currentWindowStart << "s-" << windowInfo.currentWindowEnd 
            << "s" << std::endl;
    }
    
    result.fftConsumedCount = wndEndIdx;
    return result;
}

// 基于滑动窗口检测峰值
void PeekDetector::detectPeaksInSlidingWindow(uint32_t channel, const std::vector<FFTResult>& fftResults, int wndStartIdx, int wndEndIdx, double windowStartTime, double windowEndTime) {
    
    // 确保有足够的帧用于峰值检测
    const auto& peakConfig = config_->getPeakDetectionConfig();
    if (wndEndIdx - wndStartIdx < 2 * peakConfig.timeMaxRange + 1) {
        std::cout << "[DEBUG-峰值检测] PeekDetector: 通道" << channel << "窗口内短帧数量不足: " 
                 << wndEndIdx - wndStartIdx << " < " << (2 * peakConfig.timeMaxRange + 1) << std::endl;
        return;
    }
    
    // 检测峰值
    std::vector<Peak> newPeaks = extractPeaksFromFFTResults(fftResults, wndStartIdx, wndEndIdx, windowStartTime, windowEndTime);
    
    if (!newPeaks.empty()) {
        // 详细输出每个即将添加的峰值
        std::cout << "[DEBUG-峰值添加] PeekDetector: 通道" << channel << "检测到" << newPeaks.size() 
                    << "个新峰值，详细时间戳: ";
        
        for (const auto& peak : newPeaks) {
            std::cout << peak.timestamp << "s(" << peak.frequency << "Hz," << peak.magnitude << ") ";
        }
        std::cout << std::endl;

        // 动态计算峰值数量阈值
        std::vector<std::pair<float, float>> frequencyBands = generateLogFrequencyBands(
            static_cast<float>(peakConfig.minFreq), 
            static_cast<float>(peakConfig.maxFreq), 
            peakConfig.numFrequencyBands);
        
        int dynamicPeakCount = calculateDynamicPeakCount(fftResults, wndStartIdx, wndEndIdx, frequencyBands, channel);
        
        std::cout << "[DEBUG-动态分配] PeekDetector: 通道" << channel << "动态计算峰值数量阈值: " 
                  << dynamicPeakCount << " (范围: " << peakConfig.minPeaksPerFrame 
                  << "-" << peakConfig.maxPeaksPerFrameLimit << ")" << std::endl;

        // uniquePeaks限制峰值缓存的数量，保留最强的峰值
        if (static_cast<int>(newPeaks.size()) > dynamicPeakCount) {
            this->filterPeaks(newPeaks, dynamicPeakCount, channel, peakConfig);
        }

        // 确保通道有峰值缓存
        if (peakCache_.find(channel) == peakCache_.end()) {
            peakCache_[channel] = std::vector<Peak>();
        }

        peakCache_[channel].insert(peakCache_[channel].end(), newPeaks.begin(), newPeaks.end());

        // 按时间戳排序峰值缓存
        std::sort(peakCache_[channel].begin(), peakCache_[channel].end(), 
                 [](const Peak& a, const Peak& b) { return a.timestamp < b.timestamp; });
        
    } else {
        std::cout << "[DEBUG-峰值检测] PeekDetector: 通道" << channel << "没有检测到新的峰值" << std::endl;
    }
}

void PeekDetector::filterPeaks(std::vector<Peak>& newPeaks, int maxPeaksPerFrame, uint32_t channel, const PeakDetectionConfig& peakConfig) {
    std::cout << "[DEBUG-峰值限制] PeekDetector: 通道" << channel << "峰值总数超过限制，执行频段分配策略: " 
              << newPeaks.size() << " -> " << maxPeaksPerFrame << std::endl;
    
    // 生成基于对数尺度的频段边界
    std::vector<std::pair<float, float>> frequencyBands = generateLogFrequencyBands(
        static_cast<float>(peakConfig.minFreq), 
        static_cast<float>(peakConfig.maxFreq), 
        peakConfig.numFrequencyBands);
    
    std::cout << "[DEBUG-频段分配] PeekDetector: 通道" << channel << "对数尺度频段划分(" << peakConfig.numFrequencyBands << "个): ";
    for (size_t i = 0; i < frequencyBands.size(); ++i) {
        std::cout << "频段" << i+1 << "[" << frequencyBands[i].first << "-" << frequencyBands[i].second << "Hz] ";
    }
    std::cout << std::endl;
    
    // 将峰值按频段分组
    std::vector<std::vector<Peak>> bandPeaks(peakConfig.numFrequencyBands);
    for (const auto& peak : newPeaks) {
        for (size_t i = 0; i < frequencyBands.size(); ++i) {
            if (peak.frequency >= frequencyBands[i].first && peak.frequency < frequencyBands[i].second) {
                bandPeaks[i].push_back(peak);
                break;
            }
        }
    }
    
    // 统计每个频段的峰值数量
    std::cout << "[DEBUG-频段统计] PeekDetector: 通道" << channel << "各频段峰值数量: ";
    for (size_t i = 0; i < bandPeaks.size(); ++i) {
        std::cout << "频段" << i+1 << ":" << bandPeaks[i].size() << " ";
    }
    std::cout << std::endl;
    
    // 计算频段优先级权重
    std::vector<float> bandWeights = calculateBandPriorityWeights(frequencyBands);
    
    // 计算总权重
    float totalWeight = 0.0f;
    for (float weight : bandWeights) {
        totalWeight += weight;
    }
    
    // 根据权重分配初始配额
    std::vector<int> bandQuotas(peakConfig.numFrequencyBands);
    int allocatedQuota = 0;
    
    for (size_t i = 0; i < peakConfig.numFrequencyBands; ++i) {
        bandQuotas[i] = static_cast<int>((bandWeights[i] / totalWeight) * maxPeaksPerFrame);
        allocatedQuota += bandQuotas[i];
    }
    
    // 分配剩余配额（由于整数除法可能产生的余数）
    int remainingQuota = maxPeaksPerFrame - allocatedQuota;
    
    // 按权重降序分配剩余配额
    std::vector<std::pair<float, size_t>> weightedBands;
    for (size_t i = 0; i < peakConfig.numFrequencyBands; ++i) {
        weightedBands.emplace_back(bandWeights[i], i);
    }
    std::sort(weightedBands.begin(), weightedBands.end(), 
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    for (const auto& wb : weightedBands) {
        if (remainingQuota <= 0) break;
        bandQuotas[wb.second]++;
        remainingQuota--;
    }
    
    std::cout << "[DEBUG-权重分配] PeekDetector: 通道" << channel << "频段权重和初始配额: ";
    for (size_t i = 0; i < peakConfig.numFrequencyBands; ++i) {
        float bandCenter = (frequencyBands[i].first + frequencyBands[i].second) / 2.0f;
        std::string priority = (bandCenter >= 300.0f && bandCenter <= 2500.0f) ? "中频" : 
                              (bandCenter > 2500.0f) ? "高频" : "低频";
        std::cout << "频段" << i+1 << "(" << priority << ",权重" << bandWeights[i] << ",配额" << bandQuotas[i] << ") ";
    }
    std::cout << std::endl;
    
    // 优化分配策略：处理空频段和峰值不足的频段，将多余配额收回
    std::vector<int> insufficientBands;   // 峰值数量少于配额的频段（包括空频段）
    std::vector<int> needMoreBands;       // 峰值数量超过配额的频段
    
    for (size_t i = 0; i < bandPeaks.size(); ++i) {
        int peakCount = static_cast<int>(bandPeaks[i].size());
        if (peakCount < bandQuotas[i]) {
            insufficientBands.push_back(i);
        } else if (peakCount > bandQuotas[i]) {
            needMoreBands.push_back(i);
        }
    }
    
    // 从峰值不足的频段收回多余配额
    remainingQuota = 0;
    for (int band : insufficientBands) {
        int actualPeaks = static_cast<int>(bandPeaks[band].size());
        int excessQuota = bandQuotas[band] - actualPeaks;
        remainingQuota += excessQuota;
        bandQuotas[band] = actualPeaks;  // 设置为实际峰值数量
    }
    
    // 按优先级将剩余配额分配给需要更多峰值的频段
    // 优先分配给高权重（中频）频段
    std::sort(needMoreBands.begin(), needMoreBands.end(),
              [&](int a, int b) { return bandWeights[a] > bandWeights[b]; });
    
    while (remainingQuota > 0 && !needMoreBands.empty()) {
        bool allocated = false;
        for (int band : needMoreBands) {
            if (remainingQuota <= 0) break;
            if (bandQuotas[band] < static_cast<int>(bandPeaks[band].size())) {
                bandQuotas[band]++;
                remainingQuota--;
                allocated = true;
            }
        }
        
        // 如果这一轮没有分配任何配额，说明所有频段都已满足，退出循环
        if (!allocated) break;
        
        // 移除已满足的频段
        needMoreBands.erase(
            std::remove_if(needMoreBands.begin(), needMoreBands.end(),
                [&](int band) { return bandQuotas[band] >= static_cast<int>(bandPeaks[band].size()); }),
            needMoreBands.end()
        );
    }
    
    std::cout << "[DEBUG-配额分配] PeekDetector: 通道" << channel << "最终配额分配: ";
    int totalQuota = 0;
    for (size_t i = 0; i < bandQuotas.size(); ++i) {
        std::cout << "频段" << i+1 << ":" << bandQuotas[i] << " ";
        totalQuota += bandQuotas[i];
    }
    std::cout << "(总配额:" << totalQuota << ")" << std::endl;
    
    // 在每个频段内按幅度排序并选择顶部峰值
    std::vector<Peak> filteredPeaks;
    int totalSelectedPeaks = 0;
    
    for (size_t i = 0; i < bandPeaks.size(); ++i) {
        if (bandQuotas[i] == 0 || bandPeaks[i].empty()) {
            continue;
        }
        
        // 对当前频段的峰值按幅度降序排序
        std::sort(bandPeaks[i].begin(), bandPeaks[i].end(),
            [](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
        
        // 选择该频段的顶部峰值
        int peaksToSelect = std::min(bandQuotas[i], static_cast<int>(bandPeaks[i].size()));
        for (int j = 0; j < peaksToSelect; ++j) {
            filteredPeaks.push_back(bandPeaks[i][j]);
            totalSelectedPeaks++;
        }
        
        std::cout << "[DEBUG-频段选择] PeekDetector: 通道" << channel << "频段" << i+1 
                  << "选择了" << peaksToSelect << "个峰值" << std::endl;
    }
    
    // 按时间戳排序保持时间顺序
    std::sort(filteredPeaks.begin(), filteredPeaks.end(),
        [](const Peak& a, const Peak& b) { return a.timestamp < b.timestamp; });
    
    // 用过滤后的峰值替换原始峰值
    newPeaks = std::move(filteredPeaks);
    
    std::cout << "[DEBUG-峰值限制] PeekDetector: 通道" << channel << "频段分配完成，最终保留" 
              << totalSelectedPeaks << "个峰值，详细: ";
    for (const auto& peak : newPeaks) {
        std::cout << peak.timestamp << "s(" << peak.frequency << "Hz," << peak.magnitude << ") ";
    }
    std::cout << std::endl;
}

// 从短帧FFT结果缓冲区中提取峰值
std::vector<Peak> PeekDetector::extractPeaksFromFFTResults(
    const std::vector<FFTResult>& fftResults,
    int wndStartIdx,
    int wndEndIdx,
    double windowStartTime,
    double windowEndTime) {
    
    std::vector<Peak> peaks;
    
    const auto& peakConfig = config_->getPeakDetectionConfig();
    const size_t fftSize = config_->getFFTConfig().fftSize;
    
    // 创建一个候选峰值列表
    std::vector<Peak> candidatePeaks;
    
    // 计算当前窗口中所有magnitude的分位数作为动态阈值
    std::vector<float> allMagnitudes;
    const auto startIdx = std::max(wndStartIdx, static_cast<int>(peakConfig.timeMaxRange));
    const auto endIdx = std::min(wndEndIdx, static_cast<int>(fftResults.size()) - static_cast<int>(peakConfig.timeMaxRange));
    
    // 收集窗口内所有帧的幅度值
    for (size_t frameIdx = startIdx; frameIdx < endIdx; ++frameIdx) {
        const auto& currentFrame = fftResults[frameIdx];
        for (size_t freqIdx = peakConfig.localMaxRange; 
             freqIdx < fftSize / 2 - peakConfig.localMaxRange; 
             ++freqIdx) {
            // 只收集在有效频率范围内的幅度值
            if (currentFrame.frequencies[freqIdx] >= peakConfig.minFreq && 
                currentFrame.frequencies[freqIdx] <= peakConfig.maxFreq) {
                allMagnitudes.push_back(currentFrame.magnitudes[freqIdx]);
            }
        }
    }
    
    // 计算配置的分位数
    float quantileMagnitude = 0.0f;
    if (!allMagnitudes.empty()) {
        std::sort(allMagnitudes.begin(), allMagnitudes.end());
        size_t n = allMagnitudes.size();
        
        // 计算分位数位置
        float position = peakConfig.quantileThreshold * (n - 1);
        size_t lowerIndex = static_cast<size_t>(std::floor(position));
        size_t upperIndex = static_cast<size_t>(std::ceil(position));
        
        if (lowerIndex == upperIndex) {
            quantileMagnitude = allMagnitudes[lowerIndex];
        } else {
            // 线性插值计算分位数
            float weight = position - lowerIndex;
            quantileMagnitude = allMagnitudes[lowerIndex] * (1.0f - weight) + 
                               allMagnitudes[upperIndex] * weight;
        }
    }
    
    std::cout << "[DEBUG-分位数] PeekDetector: 窗口内幅度" << (peakConfig.quantileThreshold * 100) 
              << "分位数: " << quantileMagnitude 
              << ", 最小峰值幅度阈值: " << peakConfig.minPeakMagnitude << std::endl;
    
    // 生成基于对数尺度的频段边界
    std::vector<std::pair<float, float>> frequencyBands = generateLogFrequencyBands(
        static_cast<float>(peakConfig.minFreq), 
        static_cast<float>(peakConfig.maxFreq), 
        peakConfig.numFrequencyBands);
    
    std::cout << "[DEBUG-频段划分] PeekDetector: 对数尺度频段划分(" << peakConfig.numFrequencyBands << "个): ";
    for (size_t i = 0; i < frequencyBands.size(); ++i) {
        std::cout << "频段" << i+1 << "[" << frequencyBands[i].first << "-" << frequencyBands[i].second << "Hz] ";
    }
    std::cout << std::endl;
    
    // 在每个频段内独立检测峰值
    for (size_t bandIdx = 0; bandIdx < frequencyBands.size(); ++bandIdx) {
        const auto& band = frequencyBands[bandIdx];
        std::vector<Peak> bandPeaks = extractPeaksInFrequencyBand(
            fftResults, startIdx, endIdx, band.first, band.second, 
            quantileMagnitude, peakConfig, fftSize);
        
        candidatePeaks.insert(candidatePeaks.end(), bandPeaks.begin(), bandPeaks.end());
        
        std::cout << "[DEBUG-频段峰值] PeekDetector: 频段" << bandIdx+1 
                  << "[" << band.first << "-" << band.second << "Hz] 检测到 " 
                  << bandPeaks.size() << " 个峰值" << std::endl;
    }
    
    std::cout << "[Debug] PeekDetector: 在时间窗口 " << windowStartTime << "s - " << windowEndTime 
              << "s 中检测到 " << candidatePeaks.size() << " 个候选峰值" << std::endl;
    
    return candidatePeaks;
}

// 获取峰值缓存
const std::vector<Peak>& PeekDetector::getPeakCache(uint32_t channel) const {
    static const std::vector<Peak> emptyCache;
    if (peakCache_.find(channel) != peakCache_.end()) {
        return peakCache_.at(channel);
    }
    return emptyCache;
}

// 擦除特定时间戳之前的峰值缓存
void PeekDetector::erasePeakCache(uint32_t channel, double consumedTimestamp) {
    if (peakCache_.find(channel) != peakCache_.end()) {
        auto& peaks = peakCache_[channel];
        auto it = std::find_if(peaks.begin(), peaks.end(),
            [consumedTimestamp](const Peak& peak) {
                return peak.timestamp >= consumedTimestamp;
            });
            
        if (it != peaks.begin()) {
            size_t removedCount = std::distance(peaks.begin(), it);
            peaks.erase(peaks.begin(), it);
            
            std::cout << "[DEBUG-峰值擦除] PeekDetector: 通道" << channel 
                      << "从峰值缓存中擦除了" << removedCount << "个早于" 
                      << consumedTimestamp << "s的峰值，剩余" << peaks.size() << "个峰值" << std::endl;
        }
    }
}

// 重置所有数据
void PeekDetector::reset() {
    peakCache_.clear();
    slidingWindowMap_.clear();
    std::cout << "[DEBUG-重置] PeekDetector: 已重置所有峰值缓存和滑动窗口状态" << std::endl;
}

// 生成基于对数尺度的频段边界
std::vector<std::pair<float, float>> PeekDetector::generateLogFrequencyBands(float minFreq, float maxFreq, size_t numBands) {
    std::vector<std::pair<float, float>> bands;
    
    if (numBands == 0 || minFreq >= maxFreq) {
        return bands;
    }
    
    // 使用对数尺度计算频段边界
    float logMinFreq = std::log10(minFreq);
    float logMaxFreq = std::log10(maxFreq);
    float logStep = (logMaxFreq - logMinFreq) / numBands;
    
    for (size_t i = 0; i < numBands; ++i) {
        float logStart = logMinFreq + i * logStep;
        float logEnd = logMinFreq + (i + 1) * logStep;
        
        float freqStart = std::pow(10.0f, logStart);
        float freqEnd = std::pow(10.0f, logEnd);
        
        bands.emplace_back(freqStart, freqEnd);
    }
    
    return bands;
}

// 在指定频段内检测峰值
std::vector<Peak> PeekDetector::extractPeaksInFrequencyBand(
    const std::vector<FFTResult>& fftResults,
    int startIdx, int endIdx,
    float bandMinFreq, float bandMaxFreq,
    float quantileMagnitude,
    const PeakDetectionConfig& peakConfig,
    size_t fftSize) {
    
    std::vector<Peak> bandPeaks;
    
    // 在时频域上查找局部最大值，包含首尾元素
    for (int frameIdx = startIdx; frameIdx < endIdx; ++frameIdx) {
        const auto& currentFrame = fftResults[frameIdx];
        
        // 在当前频段内查找频率bin
        for (size_t freqIdx = 0; freqIdx < fftSize / 2; ++freqIdx) {
            float currentFreq = currentFrame.frequencies[freqIdx];
            
            // 检查是否在当前频段范围内
            if (currentFreq < bandMinFreq || currentFreq > bandMaxFreq) {
                continue;
            }
            
            float currentMagnitude = currentFrame.magnitudes[freqIdx];
            
            // 检查双重幅度阈值：局部条件（大于分位数阈值）+ 全局兜底条件（大于最小峰值幅度）
            if (currentMagnitude <= quantileMagnitude || currentMagnitude < peakConfig.minPeakMagnitude) {
                continue;
            }
            
            // 检查是否在频率维度上是局部最大值 (频率域峰值检测)
            // 确保当前频率bin的幅度比其前后localMaxRange个bin的幅度都大
            bool isFreqPeak = true;
            for (size_t j = 1; j <= peakConfig.localMaxRange; ++j) {
                // 检查左边界
                if (freqIdx >= j) {
                    if (currentMagnitude <= currentFrame.magnitudes[freqIdx - j]) {
                        isFreqPeak = false;
                        break;
                    }
                }
                // 检查右边界
                if (freqIdx + j < fftSize / 2) {
                    if (currentMagnitude <= currentFrame.magnitudes[freqIdx + j]) {
                        isFreqPeak = false;
                        break;
                    }
                }
            }
            
            if (!isFreqPeak) {
                continue;
            }
            
            // 检查是否在时间维度上也是局部最大值 (时间域峰值检测)
            // 确保当前帧中的该频率bin的幅度比前后timeMaxRange个帧中的相同bin幅度都大
            bool isTimePeak = true;
            for (size_t j = 1; j <= peakConfig.timeMaxRange; ++j) {
                // 与前面的帧比较
                if (frameIdx >= static_cast<int>(j)) {
                    if (currentMagnitude <= fftResults[frameIdx - j].magnitudes[freqIdx]) {
                        isTimePeak = false;
                        break;
                    }
                }
                
                // 与后面的帧比较
                if (frameIdx + static_cast<int>(j) < static_cast<int>(fftResults.size())) {
                    if (currentMagnitude <= fftResults[frameIdx + j].magnitudes[freqIdx]) {
                        isTimePeak = false;
                        break;
                    }
                }
            }
            
            if (!isTimePeak) {
                continue;
            }
            
            // 满足所有条件，这是一个真正的时频域局部最大值
            Peak peak;
            peak.frequency = static_cast<uint32_t>(currentFreq);
            peak.magnitude = currentMagnitude;
            peak.timestamp = currentFrame.timestamp; // 使用当前短帧的精确时间戳
            
            bandPeaks.push_back(peak);

            // 添加到可视化数据（如果启用）
            if (*collectVisualizationData_) {
                visualizationData_->allPeaks.emplace_back(peak.frequency, peak.timestamp, peak.magnitude);
            }
        }
    }
    
    return bandPeaks;
}

// 计算频段优先级权重
std::vector<float> PeekDetector::calculateBandPriorityWeights(const std::vector<std::pair<float, float>>& frequencyBands) {
    std::vector<float> weights(frequencyBands.size(), 1.0f);
    
    // 定义重点频率范围：150-2500Hz (中频)
    const float priorityMinFreq = 150.0f;
    const float priorityMaxFreq = 2500.0f;
    
    for (size_t i = 0; i < frequencyBands.size(); ++i) {
        const auto& band = frequencyBands[i];
        float bandCenter = (band.first + band.second) / 2.0f;
        
        if (bandCenter >= priorityMinFreq && bandCenter <= priorityMaxFreq) {
            // 中频段：最高优先级
            weights[i] = 3.0f;
        } else if (bandCenter > priorityMaxFreq) {
            // 高频段：中等优先级
            weights[i] = 2.0f;
        } else {
            // 低频段：最低优先级
            weights[i] = 1.0f;
        }
    }
    
    return weights;
}

int PeekDetector::calculateDynamicPeakCount(const std::vector<FFTResult>& fftResults, int wndStartIdx, int wndEndIdx, const std::vector<std::pair<float, float>>& frequencyBands, uint32_t channel) {
    const auto& peakConfig = config_->getPeakDetectionConfig();
    const size_t fftSize = config_->getFFTConfig().fftSize;
    
    // 计算频段能量
    std::vector<float> bandEnergies = calculateBandEnergies(fftResults, wndStartIdx, wndEndIdx, frequencyBands, fftSize);
    
    // 估计频段噪声水平
    std::vector<float> bandNoiseLevel = estimateBandNoiseLevel(fftResults, wndStartIdx, wndEndIdx, frequencyBands, channel, fftSize);
    
    // 更新噪声历史记录
    double currentTimestamp = fftResults[wndEndIdx - 1].timestamp;
    updateNoiseHistory(channel, bandNoiseLevel, currentTimestamp);
    
    // 计算频段信噪比
    std::vector<float> bandSNR = calculateBandSNR(bandEnergies, bandNoiseLevel);
    
    // 计算总能量和平均信噪比
    float totalEnergy = 0.0f;
    float totalSNR = 0.0f;
    int validBands = 0;
    
    for (size_t i = 0; i < bandEnergies.size(); ++i) {
        totalEnergy += bandEnergies[i];
        if (bandSNR[i] > 0.0f) {  // 只计算有效的信噪比
            totalSNR += bandSNR[i];
            validBands++;
        }
    }
    
    float avgSNR = validBands > 0 ? totalSNR / validBands : 0.0f;
    
    std::cout << "[DEBUG-能量分析] PeekDetector: 通道" << channel 
              << " 总能量: " << totalEnergy 
              << ", 平均信噪比: " << avgSNR << "dB" << std::endl;
    
    // 基于能量和信噪比动态计算峰值数量
    // 能量因子：能量越高，允许更多峰值
    float energyFactor = std::min(1.0f, totalEnergy / 1000.0f);  // 假设1000为参考能量
    
    // 信噪比因子：信噪比越高，允许更多峰值
    float snrFactor = std::min(1.0f, std::max(0.0f, (avgSNR - peakConfig.snrThreshold) / 20.0f));
    
    // 综合权重计算
    float combinedFactor = peakConfig.energyWeightFactor * energyFactor + 
                          peakConfig.snrWeightFactor * snrFactor;
    
    // 计算动态峰值数量
    int dynamicCount = static_cast<int>(
        peakConfig.minPeaksPerFrame + 
        combinedFactor * (peakConfig.maxPeaksPerFrameLimit - peakConfig.minPeaksPerFrame)
    );
    
    // 确保在合理范围内
    const auto originalDynamicCount = dynamicCount;
    dynamicCount = std::max(static_cast<int>(peakConfig.minPeaksPerFrame), 
                           std::min(dynamicCount, static_cast<int>(peakConfig.maxPeaksPerFrameLimit)));
    
    std::cout << "[DEBUG-动态计算] PeekDetector: 通道" << channel 
              << " 能量因子: " << energyFactor 
              << ", 信噪比因子: " << snrFactor 
              << ", 综合因子: " << combinedFactor 
              << ", 原始动态峰值数量: " << originalDynamicCount
              << ", 动态峰值数量: " << dynamicCount << std::endl;
    
    return dynamicCount;
}

// 计算频段能量
std::vector<float> PeekDetector::calculateBandEnergies(
    const std::vector<FFTResult>& fftResults,
    int wndStartIdx, int wndEndIdx,
    const std::vector<std::pair<float, float>>& frequencyBands,
    size_t fftSize) {
    
    std::vector<float> bandEnergies(frequencyBands.size(), 0.0f);
    
    for (int frameIdx = wndStartIdx; frameIdx < wndEndIdx; ++frameIdx) {
        const auto& currentFrame = fftResults[frameIdx];
        
        for (size_t freqIdx = 0; freqIdx < fftSize / 2; ++freqIdx) {
            float currentFreq = currentFrame.frequencies[freqIdx];
            float magnitude = currentFrame.magnitudes[freqIdx];
            
            // 找到对应的频段
            for (size_t bandIdx = 0; bandIdx < frequencyBands.size(); ++bandIdx) {
                if (currentFreq >= frequencyBands[bandIdx].first && 
                    currentFreq < frequencyBands[bandIdx].second) {
                    bandEnergies[bandIdx] += magnitude * magnitude;  // 能量 = 幅度平方
                    break;
                }
            }
        }
    }
    
    // 归一化能量（除以帧数）
    int frameCount = wndEndIdx - wndStartIdx;
    if (frameCount > 0) {
        for (float& energy : bandEnergies) {
            energy /= frameCount;
        }
    }
    
    return bandEnergies;
}

// 估计频段噪声水平
std::vector<float> PeekDetector::estimateBandNoiseLevel(
    const std::vector<FFTResult>& fftResults,
    int wndStartIdx, int wndEndIdx,
    const std::vector<std::pair<float, float>>& frequencyBands,
    uint32_t channel,
    size_t fftSize) {
    
    std::vector<float> bandNoiseLevel(frequencyBands.size(), 0.0f);
    
    // 收集当前窗口每个频段的幅度值
    std::vector<std::vector<float>> currentBandMagnitudes(frequencyBands.size());
    
    for (int frameIdx = wndStartIdx; frameIdx < wndEndIdx; ++frameIdx) {
        const auto& currentFrame = fftResults[frameIdx];
        
        for (size_t freqIdx = 0; freqIdx < fftSize / 2; ++freqIdx) {
            float currentFreq = currentFrame.frequencies[freqIdx];
            float magnitude = currentFrame.magnitudes[freqIdx];
            
            // 找到对应的频段
            for (size_t bandIdx = 0; bandIdx < frequencyBands.size(); ++bandIdx) {
                if (currentFreq >= frequencyBands[bandIdx].first && 
                    currentFreq < frequencyBands[bandIdx].second) {
                    currentBandMagnitudes[bandIdx].push_back(magnitude);
                    break;
                }
            }
        }
    }
    
    // 计算当前窗口的噪声估计（使用25分位数）
    std::vector<float> currentNoiseLevel(frequencyBands.size(), 0.0f);
    for (size_t bandIdx = 0; bandIdx < frequencyBands.size(); ++bandIdx) {
        if (!currentBandMagnitudes[bandIdx].empty()) {
            std::sort(currentBandMagnitudes[bandIdx].begin(), currentBandMagnitudes[bandIdx].end());
            size_t n = currentBandMagnitudes[bandIdx].size();
            size_t noiseIndex = n / 4;  // 25分位数
            currentNoiseLevel[bandIdx] = currentBandMagnitudes[bandIdx][noiseIndex];
        }
    }
    
    // 如果有历史噪声数据，结合历史数据进行更准确的估计
    if (noiseHistory_.find(channel) != noiseHistory_.end() && !noiseHistory_[channel].empty()) {
        const auto& history = noiseHistory_[channel];
        const auto& peakConfig = config_->getPeakDetectionConfig();
        double currentTime = fftResults[wndEndIdx - 1].timestamp;
        
        std::cout << "[DEBUG-噪声估计] PeekDetector: 通道" << channel 
                  << "使用" << history.size() << "个历史噪声记录进行估计" << std::endl;
        
        // 收集有效的历史噪声数据
        std::vector<std::vector<float>> historicalNoise(frequencyBands.size());
        
        for (const auto& entry : history) {
            // 只使用时间窗口内的历史数据
            if (currentTime - entry.timestamp <= peakConfig.noiseEstimationWindow) {
                for (size_t bandIdx = 0; bandIdx < frequencyBands.size() && bandIdx < entry.bandNoiseLevel.size(); ++bandIdx) {
                    if (entry.bandNoiseLevel[bandIdx] > 0.0f) {
                        historicalNoise[bandIdx].push_back(entry.bandNoiseLevel[bandIdx]);
                    }
                }
            }
        }
        
        // 结合当前和历史数据计算最终噪声水平
        for (size_t bandIdx = 0; bandIdx < frequencyBands.size(); ++bandIdx) {
            if (!historicalNoise[bandIdx].empty()) {
                // 计算历史噪声的中位数
                std::sort(historicalNoise[bandIdx].begin(), historicalNoise[bandIdx].end());
                size_t n = historicalNoise[bandIdx].size();
                float historicalMedian = historicalNoise[bandIdx][n / 2];
                
                // 加权平均：70%历史数据 + 30%当前数据
                float historicalWeight = 0.7f;
                float currentWeight = 0.3f;
                
                if (currentNoiseLevel[bandIdx] > 0.0f) {
                    bandNoiseLevel[bandIdx] = historicalWeight * historicalMedian + 
                                            currentWeight * currentNoiseLevel[bandIdx];
                } else {
                    bandNoiseLevel[bandIdx] = historicalMedian;
                }
                
                std::cout << "[DEBUG-噪声融合] PeekDetector: 频段" << bandIdx+1 
                          << " 历史中位数: " << historicalMedian 
                          << ", 当前估计: " << currentNoiseLevel[bandIdx]
                          << ", 融合结果: " << bandNoiseLevel[bandIdx] << std::endl;
            } else {
                // 没有历史数据，使用当前估计
                bandNoiseLevel[bandIdx] = currentNoiseLevel[bandIdx];
            }
        }
    } else {
        // 没有历史数据，直接使用当前估计
        bandNoiseLevel = currentNoiseLevel;
        std::cout << "[DEBUG-噪声估计] PeekDetector: 通道" << channel 
                  << "没有历史噪声数据，使用当前窗口估计" << std::endl;
    }
    
    return bandNoiseLevel;
}

// 计算频段信噪比
std::vector<float> PeekDetector::calculateBandSNR(
    const std::vector<float>& bandEnergies,
    const std::vector<float>& bandNoiseLevel) {
    
    std::vector<float> bandSNR(bandEnergies.size(), 0.0f);
    
    for (size_t i = 0; i < bandEnergies.size(); ++i) {
        if (bandNoiseLevel[i] > 0.0f) {
            float signalPower = bandEnergies[i];
            float noisePower = bandNoiseLevel[i] * bandNoiseLevel[i];
            
            if (noisePower > 0.0f) {
                // 计算信噪比（dB）
                bandSNR[i] = 10.0f * std::log10(signalPower / noisePower);
            }
        }
    }
    
    return bandSNR;
}

// 更新噪声历史记录
void PeekDetector::updateNoiseHistory(
    uint32_t channel,
    const std::vector<float>& bandNoiseLevel,
    double timestamp) {
    
    const auto& peakConfig = config_->getPeakDetectionConfig();
    
    // 确保通道存在
    if (noiseHistory_.find(channel) == noiseHistory_.end()) {
        noiseHistory_[channel] = std::vector<NoiseHistoryEntry>();
    }
    
    // 添加新的噪声记录
    NoiseHistoryEntry entry;
    entry.timestamp = timestamp;
    entry.bandNoiseLevel = bandNoiseLevel;
    noiseHistory_[channel].push_back(entry);
    
    // 清理过期的噪声记录
    double expireTime = timestamp - peakConfig.noiseEstimationWindow;
    auto& history = noiseHistory_[channel];
    
    auto it = std::remove_if(history.begin(), history.end(),
        [expireTime](const NoiseHistoryEntry& entry) {
            return entry.timestamp < expireTime;
        });
    
    if (it != history.end()) {
        size_t removedCount = std::distance(it, history.end());
        history.erase(it, history.end());
        
        std::cout << "[DEBUG-噪声历史] PeekDetector: 通道" << channel 
                  << "清理了" << removedCount << "个过期噪声记录，剩余" 
                  << history.size() << "个记录" << std::endl;
    }
}

} // namespace afp
