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
    
    std::vector<Peak> uniquePeaks;
    if (!newPeaks.empty()) {
        // 详细输出每个即将添加的峰值
        std::cout << "[DEBUG-峰值添加] PeekDetector: 通道" << channel << "检测到" << newPeaks.size() 
                  << "个新峰值，详细时间戳: ";
        
        for (const auto& peak : newPeaks) {
            std::cout << peak.timestamp << "s(" << peak.frequency << "Hz," << peak.magnitude << ") ";
            
            // 检查是否已存在相同时间戳和频率的峰值
            bool isDuplicate = false;
            if (peakCache_.find(channel) != peakCache_.end()) {
                for (const auto& existingPeak : peakCache_[channel]) {
                    if (std::abs(existingPeak.timestamp - peak.timestamp) < 0.001 && 
                        existingPeak.frequency == peak.frequency) {
                        isDuplicate = true;
                        break;
                    }
                }
            }
            
            // 只有非重复的峰值才添加到缓存
            if (!isDuplicate) {
                uniquePeaks.push_back(peak);
                std::cout << "✓ ";  // 标记成功添加
            } else {
                std::cout << "✗(重复) ";  // 标记重复未添加
            }
        }
        std::cout << std::endl;

        // uniquePeaks限制峰值缓存的数量，保留最强的峰值
        if (uniquePeaks.size() > peakConfig.maxPeaksPerFrame) {
            std::cout << "[DEBUG-峰值限制] PeekDetector: 通道" << channel << "峰值总数超过限制，执行限制操作: " 
                      << uniquePeaks.size() << " -> " << peakConfig.maxPeaksPerFrame << std::endl;
            
            // 创建临时容器保存峰值和幅度信息
            std::vector<std::pair<size_t, float>> indexMagnitudePairs;
            indexMagnitudePairs.reserve(uniquePeaks.size());

            for (size_t i = 0; i < uniquePeaks.size(); ++i) {
                indexMagnitudePairs.emplace_back(i, uniquePeaks[i].magnitude);
            }   
            
            // 部分排序，找出幅度最大的maxPeaksPerFrame个峰值
            std::partial_sort(
                indexMagnitudePairs.begin(),
                indexMagnitudePairs.begin() + std::min(peakConfig.maxPeaksPerFrame, indexMagnitudePairs.size()),
                indexMagnitudePairs.end(),
                [](const auto& a, const auto& b) { return a.second > b.second; }
            );  

            // 提取幅度最大的maxPeaksPerFrame个峰值的索引
            std::vector<size_t> topIndices;
            topIndices.reserve(peakConfig.maxPeaksPerFrame);
            
            for (size_t i = 0; i < std::min(peakConfig.maxPeaksPerFrame, indexMagnitudePairs.size()); ++i) {
                topIndices.push_back(indexMagnitudePairs[i].first);
            }   

            // 按原始索引排序，保持时间顺序
            std::sort(topIndices.begin(), topIndices.end());

            // 创建新的峰值列表
            std::vector<Peak> filteredPeaks;
            filteredPeaks.reserve(topIndices.size());

            for (size_t idx : topIndices) {
                filteredPeaks.push_back(uniquePeaks[idx]);
            }

            // 用过滤后的峰值替换原始峰值缓存
            uniquePeaks = std::move(filteredPeaks);

            std::cout << "[DEBUG-峰值限制] PeekDetector: 通道" << channel << "限制操作保留详细: ";
            for (const auto& peak : uniquePeaks) {
                std::cout << peak.timestamp << "s(" << peak.frequency << "Hz) ";
            }
            std::cout << std::endl;
        }

        // 确保通道有峰值缓存
        if (peakCache_.find(channel) == peakCache_.end()) {
            peakCache_[channel] = std::vector<Peak>();
        }

        peakCache_[channel].insert(peakCache_[channel].end(), uniquePeaks.begin(), uniquePeaks.end());

        // 按时间戳排序峰值缓存
        std::sort(peakCache_[channel].begin(), peakCache_[channel].end(), 
                 [](const Peak& a, const Peak& b) { return a.timestamp < b.timestamp; });
        
    } else {
        std::cout << "[DEBUG-峰值检测] PeekDetector: 通道" << channel << "没有检测到新的峰值" << std::endl;
    }
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
