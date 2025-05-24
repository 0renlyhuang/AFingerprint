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

        // uniquePeaks限制峰值缓存的数量，保留最强的峰值
        if (newPeaks.size() > peakConfig.maxPeaksPerFrame) {
            this->filterPeaks(newPeaks, peakConfig.maxPeaksPerFrame, channel, peakConfig);
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
              << newPeaks.size() << " -> " << peakConfig.maxPeaksPerFrame << std::endl;
    
    // 将频率范围分为4个频段
    const float freqRange = static_cast<float>(peakConfig.maxFreq - peakConfig.minFreq);
    const float bandWidth = freqRange / 4.0f;
    
    // 定义4个频段的边界
    std::vector<std::pair<float, float>> frequencyBands = {
        {static_cast<float>(peakConfig.minFreq), static_cast<float>(peakConfig.minFreq) + bandWidth},
        {static_cast<float>(peakConfig.minFreq) + bandWidth, static_cast<float>(peakConfig.minFreq) + 2 * bandWidth},
        {static_cast<float>(peakConfig.minFreq) + 2 * bandWidth, static_cast<float>(peakConfig.minFreq) + 3 * bandWidth},
        {static_cast<float>(peakConfig.minFreq) + 3 * bandWidth, static_cast<float>(peakConfig.maxFreq)}
    };
    
    std::cout << "[DEBUG-频段分配] PeekDetector: 通道" << channel << "频段划分: ";
    for (size_t i = 0; i < frequencyBands.size(); ++i) {
        std::cout << "频段" << i+1 << "[" << frequencyBands[i].first << "-" << frequencyBands[i].second << "Hz] ";
    }
    std::cout << std::endl;
    
    // 将峰值按频段分组
    std::vector<std::vector<Peak>> bandPeaks(4);
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
    
    // 初始分配：每个频段平均分配峰值配额
    const int baseQuotaPerBand = peakConfig.maxPeaksPerFrame / 4;
    std::vector<int> bandQuotas(4, baseQuotaPerBand);
    int remainingQuota = peakConfig.maxPeaksPerFrame - (baseQuotaPerBand * 4);
    
    // 优化分配策略：
    // 1. 处理空频段和峰值不足的频段，将多余配额收回
    // 2. 将收回的配额重新分配给有更多峰值的频段
    
    // 第一轮：收集各种类型的频段
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
    
    // 第二轮：从峰值不足的频段收回多余配额
    for (int band : insufficientBands) {
        int actualPeaks = static_cast<int>(bandPeaks[band].size());
        int excessQuota = bandQuotas[band] - actualPeaks;
        remainingQuota += excessQuota;
        bandQuotas[band] = actualPeaks;  // 设置为实际峰值数量
    }
    
    // 第三轮：将剩余配额分配给需要更多峰值的频段
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
    
    // 在时频域上查找局部最大值
    // 跳过开始和结束的timeMaxRange个帧，以便在时间维度上能进行完整比较
    

    // 根据wndStartIdx前面的元素数量，确定时间维度上需要跳过的元素数量
    const auto startIdx = std::max(wndStartIdx, static_cast<int>(peakConfig.timeMaxRange));
    for (size_t frameIdx = startIdx; frameIdx < wndEndIdx; ++frameIdx) {
        
        const auto& currentFrame = fftResults[frameIdx];
        
        // 跳过频谱边缘的频率bin，以便在频率维度上比较
        for (size_t freqIdx = peakConfig.localMaxRange; 
             freqIdx < fftSize / 2 - peakConfig.localMaxRange; 
             ++freqIdx) {
            
            // 检查频率范围
            if (currentFrame.frequencies[freqIdx] < peakConfig.minFreq || 
                currentFrame.frequencies[freqIdx] > peakConfig.maxFreq) {
                continue;
            }
            
            float currentMagnitude = currentFrame.magnitudes[freqIdx];
            
            // 检查最小幅度阈值
            if (currentMagnitude < peakConfig.minPeakMagnitude) {
                continue;
            }
            
            // 检查是否在频率维度上是局部最大值 (频率域峰值检测)
            // 确保当前频率bin的幅度比其前后localMaxRange个bin的幅度都大
            bool isFreqPeak = true;
            for (size_t j = 1; j <= peakConfig.localMaxRange; ++j) {
                if (currentMagnitude <= currentFrame.magnitudes[freqIdx - j] || 
                    currentMagnitude <= currentFrame.magnitudes[freqIdx + j]) {
                    isFreqPeak = false;
                    break;
                }
            }
            
            if (!isFreqPeak) {
                continue;
            }
            
            // 改进：检查是否在时间维度上也是局部最大值 (时间域峰值检测)
            // 确保当前帧中的该频率bin的幅度比前后timeMaxRange个帧中的相同bin幅度都大
            bool isTimePeak = true;
            for (size_t j = 1; j <= peakConfig.timeMaxRange; ++j) {
                // 与前面的帧比较
                if (currentMagnitude <= fftResults[frameIdx - j].magnitudes[freqIdx]) {
                    isTimePeak = false;
                    break;
                }
                
                // 与后面的帧比较
                if (currentMagnitude <= fftResults[frameIdx + j].magnitudes[freqIdx]) {
                    isTimePeak = false;
                    break;
                }
            }
            
            if (!isTimePeak) {
                continue;
            }
            
            // 满足所有条件，这是一个真正的时频域局部最大值
            Peak peak;
            peak.frequency = static_cast<uint32_t>(currentFrame.frequencies[freqIdx]);
            peak.magnitude = currentMagnitude;
            peak.timestamp = currentFrame.timestamp; // 使用当前短帧的精确时间戳
            
            candidatePeaks.push_back(peak);

            // 添加到可视化数据（如果启用）
            if (*collectVisualizationData_) {
                visualizationData_->allPeaks.emplace_back(peak.frequency, peak.timestamp, peak.magnitude);
            }
        }
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

} // namespace afp
