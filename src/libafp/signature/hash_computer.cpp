#include "hash_computer.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace afp {

HashComputer::HashComputer(std::shared_ptr<IPerformanceConfig> config, bool* collectVisualizationData, VisualizationData* visualizationData)
    : config_(config), collectVisualizationData_(collectVisualizationData), visualizationData_(visualizationData) {
}

HashComputer::~HashComputer() = default;

// 计算哈希值
HashComputer::ComputeHashReturn HashComputer::computeHash(
    const std::deque<Frame>& longFrames, 
    std::vector<SignaturePoint>& signatures) {
    
    ComputeHashReturn result;
    result.isHashComputed = false;
    result.consumedFrameCount = 0;
    
    // 获取指纹生成配置
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    const size_t symmetricRange = signatureConfig.symmetricFrameRange;
    
    // 确保我们有足够的帧进行扩展选取
    // 需要至少 2*symmetricRange + 1 帧才能进行一次完整的对称选取
    size_t minRequiredFrames = 2 * symmetricRange + 1;
    if (longFrames.size() < minRequiredFrames) {
        std::cout << "[DEBUG-指纹生成] HashComputer: 帧历史不足" << minRequiredFrames 
                  << "帧(对称范围=" << symmetricRange << ")，无法生成指纹，当前帧数:" 
                  << longFrames.size() << std::endl;
        return result;
    }

    // 使用滑动窗口方式处理，但现在使用扩展的对称选取
    size_t totalAcceptedCombinations = 0;
    
    // 计算可以进行处理的锚点位置范围
    // 锚点必须距离边界至少symmetricRange的距离
    size_t firstAnchorIndex = symmetricRange;
    size_t lastAnchorIndex = longFrames.size() - symmetricRange - 1;
    
    std::cout << "[DEBUG-指纹生成] HashComputer: 使用对称范围" << symmetricRange 
              << "，锚点范围[" << firstAnchorIndex << "," << lastAnchorIndex 
              << "]，开始扩展三帧选取" << std::endl;

    // 遍历所有可能的锚点位置
    for (size_t anchorIndex = firstAnchorIndex; anchorIndex <= lastAnchorIndex; anchorIndex++) {
        std::cout << "[DEBUG-指纹生成] HashComputer: 处理锚点位置=" << anchorIndex << std::endl;

        // 生成对称的三帧组合：从(x-n, x, x+n)到(x-1, x, x+1)
        for (size_t distance = 1; distance <= symmetricRange; distance++) {
            size_t frame1Index = anchorIndex - distance;  // 左侧帧
            size_t frame2Index = anchorIndex;             // 锚点帧
            size_t frame3Index = anchorIndex + distance;  // 右侧帧
            
            const Frame& frame1 = longFrames[frame1Index];
            const Frame& frame2 = longFrames[frame2Index]; 
            const Frame& frame3 = longFrames[frame3Index];
            
            std::cout << "[DEBUG-指纹生成] HashComputer: 处理距离=" << distance 
                      << "的三帧组合，帧索引[" << frame1Index << "," << frame2Index << "," << frame3Index 
                      << "]，时间戳[" << frame1.timestamp << "s," << frame2.timestamp << "s," 
                      << frame3.timestamp << "s]" << std::endl;
            
            // 统计不同原因的过滤数量
            size_t totalPossibleCombinations = 0;
            size_t filteredByFreqDelta1_min = 0;
            size_t filteredByFreqDelta1_max = 0;
            size_t filteredByTimeDelta1 = 0;
            size_t filteredByFreqDelta2 = 0;
            size_t filteredByTimeDelta2 = 0;
            size_t filteredByFreqDeltaSimilarity = 0;
            size_t filteredByLowScore = 0;
            size_t validCombinations = 0;
            size_t acceptedCombinations = 0;
            
            // 潜在组合总数
            size_t theoreticalCombinations = frame1.peaks.size() * frame2.peaks.size() * frame3.peaks.size();
            
            // 跳过包含空帧的窗口
            if (frame1.peaks.empty() || frame2.peaks.empty() || frame3.peaks.empty()) {
                std::cout << "[警告-指纹生成] HashComputer: 锚点" << anchorIndex  << "，距离" << distance << "，"
                          << "存在空帧，跳过此窗口" << std::endl;
                continue;
            }
            
            std::cout << "[DEBUG-指纹生成] HashComputer: 锚点" << anchorIndex  << "，距离" << distance << "，"
                      << "理论可能的峰值组合数: " << theoreticalCombinations << " (帧1:"
                      << frame1.peaks.size() << "峰值, 帧2:" << frame2.peaks.size() 
                      << "峰值, 帧3:" << frame3.peaks.size() << "峰值)" << std::endl;
            
            // 收集所有有效的峰值组合并计算评分
            std::vector<ScoredTripleFrameCombination> validCombinationsVec;
            
            // 从中间帧选择锚点峰值
            for (const auto& anchorPeak : frame2.peaks) {
                // 从第一帧（最旧）和第三帧（最新）中选择目标峰值
                for (const auto& targetPeak1 : frame1.peaks) {
                    // 计算所有可能的组合数
                    totalPossibleCombinations += frame3.peaks.size();
                    
                    // 计算第一个频率差并检查是否在有效范围内
                    int32_t freqDelta1 = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak1.frequency);
                    if (std::abs(freqDelta1) < signatureConfig.minFreqDelta) {
                        filteredByFreqDelta1_min += frame3.peaks.size();
                        continue; // 跳过频率差太小
                    }
                    if (std::abs(freqDelta1) > signatureConfig.maxFreqDelta) {
                        filteredByFreqDelta1_max += frame3.peaks.size();
                        continue; // 跳过频率差太大
                    }
                    
                    // 检查时间差是否在有效范围内
                    double timeDelta1 = anchorPeak.timestamp - targetPeak1.timestamp;
                    if (std::abs(timeDelta1) > signatureConfig.maxTimeDelta) {
                        filteredByTimeDelta1 += frame3.peaks.size();
                        continue; // 跳过时间差太大的配对
                    }

                    for (const auto& targetPeak2 : frame3.peaks) {
                        // 计算第二个频率差并检查是否在有效范围内
                        int32_t freqDelta2 = static_cast<int32_t>(targetPeak2.frequency) - static_cast<int32_t>(anchorPeak.frequency);
                        if (std::abs(freqDelta2) < signatureConfig.minFreqDelta || 
                            std::abs(freqDelta2) > signatureConfig.maxFreqDelta) {
                            filteredByFreqDelta2++;
                            continue; // 跳过频率差太小或太大的配对
                        }
                        
                        // 检查时间差是否在有效范围内
                        double timeDelta2 = targetPeak2.timestamp - anchorPeak.timestamp;
                        if (std::abs(timeDelta2) > signatureConfig.maxTimeDelta) {
                            filteredByTimeDelta2++;
                            continue; // 跳过时间差太大的配对
                        }
                        
                        // 确保频率差之间有足够的差异，避免生成类似的哈希值
                        if (std::abs(freqDelta1 - freqDelta2) < signatureConfig.minFreqDelta / 2) {
                            filteredByFreqDeltaSimilarity++;
                            continue; // 两个频率差太相似
                        }
                        
                        // 计算评分
                        double score = computeTripleFrameCombinationScore(anchorPeak, targetPeak1, targetPeak2);
                        
                        // 检查评分是否满足最低阈值
                        if (score < signatureConfig.minTripleFrameScore) {
                            filteredByLowScore++;
                            continue; // 跳过评分过低的组合
                        }
                        
                        // 计算三帧组合哈希值，使用峰值的实际时间戳，而不是帧的时间戳
                        uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);
                        
                        // 添加到有效组合列表
                        ScoredTripleFrameCombination combination;
                        combination.anchorPeak = &anchorPeak;
                        combination.targetPeak1 = &targetPeak1;
                        combination.targetPeak2 = &targetPeak2;
                        combination.score = score;
                        combination.hash = hash;
                        
                        validCombinationsVec.push_back(combination);
                        validCombinations++;
                    }
                }
            }
            
            // 按评分排序，保留topN
            std::sort(validCombinationsVec.begin(), validCombinationsVec.end(), std::greater<ScoredTripleFrameCombination>());
            
            // 限制保留的组合数量
            size_t maxCombinations = std::min(validCombinationsVec.size(), signatureConfig.maxTripleFrameCombinations);
            acceptedCombinations = 0;
            
            // 生成签名点
            for (size_t i = 0; i < maxCombinations; i++) {
                const auto& combination = validCombinationsVec[i];
                
                // 创建签名点
                SignaturePoint signaturePoint;
                signaturePoint.hash = combination.hash;
                signaturePoint.timestamp = combination.anchorPeak->timestamp; // 使用锚点峰值的精确时间戳
                signaturePoint.frequency = combination.anchorPeak->frequency;
                signaturePoint.amplitude = static_cast<uint32_t>(combination.anchorPeak->magnitude * 1000);
                
                // Add to visualization data if enabled
                if (*collectVisualizationData_) {
                    visualizationData_->fingerprintPoints.emplace_back(
                        signaturePoint.frequency, 
                        signaturePoint.timestamp, 
                        signaturePoint.hash
                    );
                }

                signatures.push_back(signaturePoint);
                acceptedCombinations++;
            }
            
            totalAcceptedCombinations += acceptedCombinations;
            
            // 输出窗口过滤统计信息
            if (totalPossibleCombinations > 0) {
                std::cout << "[DEBUG-指纹生成] HashComputer: 锚点" << anchorIndex  << "，距离" << distance << "，"
                          << "总可能组合: " << totalPossibleCombinations 
                          << ", 有效组合: " << validCombinations
                          << ", 接受: " << acceptedCombinations 
                          << " (" << (acceptedCombinations * 100.0 / totalPossibleCombinations) << "%)" << std::endl;
                
                // 详细的过滤统计
                if (validCombinations > 0) {
                    std::cout << "[DEBUG-指纹生成] HashComputer: 锚点" << anchorIndex  << "，距离" << distance << "，"
                              << "过滤分布 - FreqDelta1_min: " << filteredByFreqDelta1_min << " (" << (filteredByFreqDelta1_min * 100.0 / totalPossibleCombinations) << "%)"
                              << ", FreqDelta1_max: " << filteredByFreqDelta1_max << " (" << (filteredByFreqDelta1_max * 100.0 / totalPossibleCombinations) << "%)"
                              << ", TimeDelta1: " << filteredByTimeDelta1 << " (" << (filteredByTimeDelta1 * 100.0 / totalPossibleCombinations) << "%)"
                              << ", FreqDelta2: " << filteredByFreqDelta2 << " (" << (filteredByFreqDelta2 * 100.0 / totalPossibleCombinations) << "%)"
                              << ", TimeDelta2: " << filteredByTimeDelta2 << " (" << (filteredByTimeDelta2 * 100.0 / totalPossibleCombinations) << "%)"
                              << ", FreqDeltaSimilarity: " << filteredByFreqDeltaSimilarity << " (" << (filteredByFreqDeltaSimilarity * 100.0 / totalPossibleCombinations) << "%)"
                              << ", LowScore: " << filteredByLowScore << " (" << (filteredByLowScore * 100.0 / totalPossibleCombinations) << "%)"
                              << ", TopN筛选: " << (validCombinations - acceptedCombinations) << " (" << ((validCombinations - acceptedCombinations) * 100.0 / totalPossibleCombinations) << "%)"
                              << std::endl;
                }
            } else {
                std::cout << "[警告-指纹生成] HashComputer: 锚点" << anchorIndex  << "，距离" << distance << "，"
                          << "没有可能的峰值组合，无法生成指纹" << std::endl;
            }
        }
    }
    
    // 总体统计输出
    std::cout << "[DEBUG-指纹生成] HashComputer: 所有窗口处理完毕，共生成" 
              << totalAcceptedCombinations << "个指纹点" << std::endl;
    
    // 设置结果
    result.isHashComputed = totalAcceptedCombinations > 0;
    
    // 计算可以安全消费的帧数
    // 由于我们现在处理了多个锚点位置，更加保守地消费帧
    // 只消费一个锚点位置的帧，确保下次仍有足够的帧可用
    if (result.isHashComputed) {
        // 现在处理了多个锚点，可以消费更多帧，但仍要保守
        // 消费的帧数等于已处理的锚点数量，但至少保留 symmetricRange 个帧用于下次计算
        size_t processedAnchors = lastAnchorIndex - firstAnchorIndex + 1;
        result.consumedFrameCount = processedAnchors;
        
        std::cout << "[DEBUG-指纹生成] HashComputer: 处理了" << processedAnchors << "个锚点位置，生成" 
                  << totalAcceptedCombinations << "个指纹点，建议消费" << result.consumedFrameCount << "帧" << std::endl;
    }
    
    return result;
}

// 计算哈希值
HashComputer::ComputeHashReturn HashComputer::computeHash2(
    const std::deque<Frame>& longFrames, 
    std::vector<SignaturePoint>& signatures) {
    
    ComputeHashReturn result;
    result.isHashComputed = false;
    result.consumedFrameCount = 0;
    
    // 确保我们有至少2帧
    if (longFrames.size() < 2) {
        std::cout << "[DEBUG-指纹生成] HashComputer: 帧历史不足2帧，无法生成指纹" << std::endl;
        return result;
    }

    // 获取指纹生成配置
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    
    // 使用滑动窗口方式处理所有可能的双帧组合
    size_t totalAcceptedCombinations = 0;
    
    // 计算可能的滑动窗口数量
    size_t numWindows = longFrames.size() - 1; // 至少需要2帧一组
    
    std::cout << "[DEBUG-指纹生成] HashComputer: 发现" << numWindows 
              << "个可能的双帧窗口组合，开始处理" << std::endl;
    
    // 对每个可能的双帧窗口进行处理
    for (size_t windowStart = 0; windowStart < numWindows; windowStart++) {
        // 获取当前窗口的两帧
        const Frame& frame1 = longFrames[windowStart];     // 窗口中最旧的帧
        const Frame& frame2 = longFrames[windowStart + 1]; // 窗口中最新的帧
        
        std::cout << "[DEBUG-指纹生成] HashComputer: 处理第" << (windowStart + 1) << "/"
                  << numWindows << "个窗口，帧时间戳: " << frame1.timestamp << "s, "
                  << frame2.timestamp << "s" << std::endl;
        
        // 统计不同原因的过滤数量
        size_t totalPossibleCombinations = 0;
        size_t filteredByFreqDelta_min = 0;
        size_t filteredByFreqDelta_max = 0;
        size_t filteredByTimeDelta = 0;
        size_t filteredByLowScore = 0;
        size_t validCombinations = 0;
        
        // 潜在组合总数
        size_t theoreticalCombinations = frame1.peaks.size() * frame2.peaks.size();
        
        // 跳过包含空帧的窗口
        if (frame1.peaks.empty() || frame2.peaks.empty()) {
            std::cout << "[警告-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                      << "存在空帧，跳过此窗口" << std::endl;
            continue;
        }
        
        std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                  << "理论可能的峰值组合数: " << theoreticalCombinations << " (帧1:"
                  << frame1.peaks.size() << "峰值, 帧2:" << frame2.peaks.size() 
                  << "峰值)" << std::endl;
        
        // 收集所有有效的峰值组合并计算评分
        std::vector<ScoredPeakCombination> validCombinationsVec;
        
        // 从第一帧选择锚点峰值
        for (const auto& anchorPeak : frame1.peaks) {
            // 从第二帧选择目标峰值
            for (const auto& targetPeak : frame2.peaks) {
                totalPossibleCombinations++;

                // 计算频率差并检查是否在有效范围内
                int32_t freqDelta = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak.frequency);
                if (std::abs(freqDelta) < signatureConfig.minFreqDelta) {
                    filteredByFreqDelta_min++;
                    continue; // 跳过频率差太小
                }
                if (std::abs(freqDelta) > signatureConfig.maxFreqDelta) {
                    filteredByFreqDelta_max++;
                    continue; // 跳过频率差太大
                }
                
                // 检查时间差是否在有效范围内
                double timeDelta = anchorPeak.timestamp - targetPeak.timestamp;
                if (std::abs(timeDelta) > signatureConfig.maxTimeDelta) {
                    filteredByTimeDelta++;
                    continue; // 跳过时间差太大的配对
                }

                // 计算评分
                double score = computeDoubleFrameCombinationScore(anchorPeak, targetPeak);
                
                // 检查评分是否满足最低阈值
                if (score < signatureConfig.minDoubleFrameScore) {
                    filteredByLowScore++;
                    continue; // 跳过评分过低的组合
                }
                
                // 计算哈希值
                uint32_t hash = computeDoubleFrameHash(anchorPeak, targetPeak);
                
                // 添加到有效组合列表
                ScoredPeakCombination combination;
                combination.anchorPeak = &anchorPeak;
                combination.targetPeak = &targetPeak;
                combination.score = score;
                combination.hash = hash;
                
                validCombinationsVec.push_back(combination);
                validCombinations++;
            }
        }
        
        // 按评分排序，保留topN
        std::sort(validCombinationsVec.begin(), validCombinationsVec.end(), std::greater<ScoredPeakCombination>());
        
        // 限制保留的组合数量
        size_t maxCombinations = std::min(validCombinationsVec.size(), signatureConfig.maxDoubleFrameCombinations);
        size_t acceptedCombinations = 0;
        
        // 生成签名点
        for (size_t i = 0; i < maxCombinations; i++) {
            const auto& combination = validCombinationsVec[i];
            
            // 创建签名点
            SignaturePoint signaturePoint;
            signaturePoint.hash = combination.hash;
            signaturePoint.timestamp = combination.anchorPeak->timestamp; // 使用锚点峰值的精确时间戳
            signaturePoint.frequency = combination.anchorPeak->frequency;
            signaturePoint.amplitude = static_cast<uint32_t>(combination.anchorPeak->magnitude * 1000);
            
            // Add to visualization data if enabled
            if (*collectVisualizationData_) {
                visualizationData_->fingerprintPoints.emplace_back(
                    signaturePoint.frequency, 
                    signaturePoint.timestamp, 
                    signaturePoint.hash
                );
            }

            signatures.push_back(signaturePoint);
            acceptedCombinations++;
        }
        
        totalAcceptedCombinations += acceptedCombinations;
        
        // 输出窗口过滤统计信息
        if (totalPossibleCombinations > 0) {
            std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                      << "总可能组合: " << totalPossibleCombinations 
                      << ", 有效组合: " << validCombinations
                      << ", 接受: " << acceptedCombinations 
                      << " (" << (acceptedCombinations * 100.0 / totalPossibleCombinations) << "%)" << std::endl;
            
            // 详细的过滤统计
            if (validCombinations > 0) {
                std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                          << "过滤分布 - FreqDelta_min: " << filteredByFreqDelta_min << " (" << (filteredByFreqDelta_min * 100.0 / totalPossibleCombinations) << "%)"
                          << ", FreqDelta_max: " << filteredByFreqDelta_max << " (" << (filteredByFreqDelta_max * 100.0 / totalPossibleCombinations) << "%)"
                          << ", TimeDelta: " << filteredByTimeDelta << " (" << (filteredByTimeDelta * 100.0 / totalPossibleCombinations) << "%)"
                          << ", LowScore: " << filteredByLowScore << " (" << (filteredByLowScore * 100.0 / totalPossibleCombinations) << "%)"
                          << ", TopN筛选: " << (validCombinations - acceptedCombinations) << " (" << ((validCombinations - acceptedCombinations) * 100.0 / totalPossibleCombinations) << "%)"
                          << std::endl;
            }
        } else {
            std::cout << "[警告-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                      << "没有可能的峰值组合，无法生成指纹" << std::endl;
        }
    }
    
    // 总体统计输出
    std::cout << "[DEBUG-指纹生成] HashComputer: 所有窗口处理完毕，共生成" 
              << totalAcceptedCombinations << "个指纹点" << std::endl;
    
    // 设置结果
    result.isHashComputed = totalAcceptedCombinations > 0;
    
    // 对于双帧方法，也设置消费帧数量
    if (result.isHashComputed) {
        result.consumedFrameCount = 1; // 双帧方法每次消费1帧
    }
    
    return result;
}


// 计算三帧组合哈希值
uint32_t HashComputer::computeTripleFrameHash(
    const Peak& anchorPeak,
    const Peak& targetPeak1,
    const Peak& targetPeak2) {

    // 时间差最多是0.16s，粒度是0.1s，因此4位足够；进一步兼容，粒度放大到0.2s, 3位就足够了
    // 频率差最大是3000Hz，最多需要12位，进一步兼容，粒度放大到4Hz, 10位就足够了，加上符号位，最多需要11位
    // 频率最大是5000Hz，进一步兼容，粒度放大到2Hz, 最多需要12位
    // 32位hash组成：
    // [31:20] 锚点频率 (12位)        - 位置20-31, 锚点频率 除以 4Hz
    // [19:10] combo1 (10位)         - 位置10-19  
    //     [19:10] anchor-target1 频率差绝对值 (10位)     - 频率差绝对值 除以 4Hz
    //        异或区域[10:10] anchor-target1的符号位(1位置) - 位置10，如果anchor-target1是负数，则该位置为1，否则为0
    //        异或区域[14:11] anchor-target1的时间差 (4位) - 位置11-14, 时间差 除以 0.2s
    //     将符号信息和时间差信息异或叠加到频率差信息的[14:10]上，得到combo1
    // [9:0]  combo2 (10位)         - 位置0-9
    //     [9:0] anchor-target2 频率差绝对值 (10位)     - 频率差绝对值 除以 4Hz
    //        异或区域[0:0] anchor-target2的符号位(1位置) - 位置0，如果anchor-target2是负数，则该位置为1，否则为0
    //        异或区域[4:1] anchor-target2的时间差 (4位) - 位置1-4, 时间差 除以 0.2s
    //     将符号信息和时间差信息异或叠加到频率差信息的[4:0]上，得到combo2
    // 因此需要12+10+10=32位
    
    // 1. 计算锚点频率 (12位，位置20-31)
    uint32_t anchorFreqQuantized = (anchorPeak.frequency / 4) & 0xFFF; // 除以4Hz，限制为12位
    
    // 2. 计算combo1 (anchor-target1的组合)
    int32_t freqDelta1 = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak1.frequency);
    uint32_t freqDelta1Abs = (static_cast<uint32_t>(std::abs(freqDelta1)) / 4) & 0x3FF; // 除以4Hz，限制为10位
    uint32_t freqDelta1Sign = (freqDelta1 < 0) ? 1 : 0; // 符号位
    
    double timeDelta1 = anchorPeak.timestamp - targetPeak1.timestamp;
    uint32_t timeDelta1Quantized = static_cast<uint32_t>(std::max(0.0, std::min(15.0, std::abs(timeDelta1) / 0.2))) & 0xF; // 除以0.2s，限制为4位
    
    // 构建combo1：将符号位和时间差信息异或到频率差信息的低5位[4:0]
    uint32_t combo1 = freqDelta1Abs;
    uint32_t timeSignCombo1 = (freqDelta1Sign) | (timeDelta1Quantized << 1); // 符号位(1位) + 时间差(4位) = 5位
    combo1 ^= timeSignCombo1; // 异或到频率差的低5位
    combo1 &= 0x3FF; // 确保只有10位
    
    // 3. 计算combo2 (anchor-target2的组合)
    int32_t freqDelta2 = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak2.frequency);
    uint32_t freqDelta2Abs = (static_cast<uint32_t>(std::abs(freqDelta2)) / 4) & 0x3FF; // 除以4Hz，限制为10位
    uint32_t freqDelta2Sign = (freqDelta2 < 0) ? 1 : 0; // 符号位
    
    double timeDelta2 = anchorPeak.timestamp - targetPeak2.timestamp;
    uint32_t timeDelta2Quantized = static_cast<uint32_t>(std::max(0.0, std::min(15.0, std::abs(timeDelta2) / 0.2))) & 0xF; // 除以0.2s，限制为4位
    
    // 构建combo2：将符号位和时间差信息异或到频率差信息的低5位[4:0]
    uint32_t combo2 = freqDelta2Abs;
    uint32_t timeSignCombo2 = (freqDelta2Sign) | (timeDelta2Quantized << 1); // 符号位(1位) + 时间差(4位) = 5位
    combo2 ^= timeSignCombo2; // 异或到频率差的低5位
    combo2 &= 0x3FF; // 确保只有10位
    
    // 4. 组合32位哈希值
    // [31:20] 锚点频率(12位) | [19:10] combo1(10位) | [9:0] combo2(10位)
    uint32_t hash = (anchorFreqQuantized << 20) |  // 锚点频率 (12位) - 位置20-31
                   (combo1 << 10) |               // combo1 (10位) - 位置10-19
                   combo2;                        // combo2 (10位) - 位置0-9
    
    return hash;
}

// 计算三帧组合哈希值
uint32_t HashComputer::computeDoubleFrameHash(
    const Peak& anchorPeak,
    const Peak& targetPeak) {
    
    // 时间差最多是0.16s，粒度是0.1s，因此4位足够
    // 频率差最大是3000Hz，最多需要12位，加上符号位，最多需要13位
    // 频率最大是5000Hz，最多需要13位
    // 因此需要4+13+13=30位
    
    // 1. 计算锚点频率（前13位，位置19-31）
    uint32_t anchorFreq = anchorPeak.frequency & 0x1FFF; // 限制为13位，最大8191Hz
    
    // 2. 计算频率差（13位：1位符号 + 12位幅度，位置6-18）
    int32_t freqDelta = static_cast<int32_t>(targetPeak.frequency) - static_cast<int32_t>(anchorPeak.frequency);
    uint32_t freqDeltaAbs = static_cast<uint32_t>(std::abs(freqDelta)) & 0xFFF; // 限制为12位，最大4095Hz
    uint32_t freqDeltaSign = (freqDelta < 0) ? 1 : 0; // 符号位：负数为1，正数为0
    uint32_t freqDeltaEncoded = (freqDeltaSign << 12) | freqDeltaAbs; // 13位：符号位 + 12位幅度
    
    // 3. 计算时间差（4位，位置2-5）
    double timeDelta = targetPeak.timestamp - anchorPeak.timestamp;
    // 将时间差转换为0.1s为单位的整数，范围[-0.8s, 0.8s] -> [-8, 8] -> [0, 16]
    int32_t timeDeltaQuantized = static_cast<int32_t>(std::round(timeDelta * 10.0)); // 转换为0.1s单位
    timeDeltaQuantized = std::max(-8, std::min(8, timeDeltaQuantized)); // 限制范围
    uint32_t timeDeltaEncoded = static_cast<uint32_t>(timeDeltaQuantized + 8) & 0xF; // 转换为无符号4位：[0, 16] -> [0, 15]
    
    // 4. 组合32位哈希值
    // 位布局：[31:19] 锚点频率(13位) | [18:6] 频率差(13位) | [5:2] 时间差(4位) | [1:0] 填充0(2位)
    uint32_t hash = (anchorFreq << 19) |           // 锚点频率，位置19-31
                   (freqDeltaEncoded << 6) |       // 频率差，位置6-18
                   (timeDeltaEncoded << 2) |       // 时间差，位置2-5
                   0;                              // 最后2位填充0

    return hash;
}

// 计算双帧峰值组合的评分
double HashComputer::computeDoubleFrameCombinationScore(
    const Peak& anchorPeak,
    const Peak& targetPeak) {
    
    double score = 0.0;
    
    // 1. 幅度评分 (40% 权重) - 优先选择幅度较大的峰值组合
    // 使用几何平均值来平衡两个峰值的幅度
    double magnitudeScore = std::sqrt(anchorPeak.magnitude * targetPeak.magnitude);
    score += magnitudeScore * 0.4;
    
    // 2. 频率差稳定性评分 (25% 权重) - 优先选择频率差适中的组合
    int32_t freqDelta = std::abs(static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak.frequency));
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    
    // 计算频率差在有效范围内的归一化位置 [0, 1]
    double freqDeltaNormalized = static_cast<double>(freqDelta - signatureConfig.minFreqDelta) / 
                                (signatureConfig.maxFreqDelta - signatureConfig.minFreqDelta);
    
    // 使用倒置的二次函数，使中等频率差获得更高分数
    // f(x) = 1 - 4*(x-0.5)^2，在x=0.5时达到最大值1
    double freqDeltaScore = 1.0 - 4.0 * std::pow(freqDeltaNormalized - 0.5, 2);
    freqDeltaScore = std::max(0.0, freqDeltaScore); // 确保非负
    score += freqDeltaScore * 25.0 * 0.25; // 乘以25是为了将分数范围调整到合理区间
    
    // 3. 时间差稳定性评分 (20% 权重) - 优先选择时间差较小的组合
    double timeDelta = std::abs(anchorPeak.timestamp - targetPeak.timestamp);
    double timeDeltaNormalized = timeDelta / signatureConfig.maxTimeDelta; // [0, 1]
    
    // 时间差越小，分数越高
    double timeDeltaScore = (1.0 - timeDeltaNormalized) * 10.0; // 乘以10调整分数范围
    score += timeDeltaScore * 0.2;
    
    // 4. 频率位置评分 (10% 权重) - 优先选择中频段的峰值
    // 人耳对中频段(1000-3000Hz)更敏感，这些频段的特征更稳定
    double avgFreq = (anchorPeak.frequency + targetPeak.frequency) / 2.0;
    double freqPositionScore = 0.0;
    
    if (avgFreq >= 1000 && avgFreq <= 3000) {
        // 中频段获得最高分
        freqPositionScore = 10.0;
    } else if (avgFreq >= 500 && avgFreq <= 4000) {
        // 次优频段获得中等分数
        freqPositionScore = 7.0;
    } else {
        // 其他频段获得较低分数
        freqPositionScore = 3.0;
    }
    score += freqPositionScore * 0.1;
    
    // 5. 峰值尖锐度评分 (5% 权重) - 优先选择更尖锐的峰值
    // 尖锐的峰值通常更稳定，不容易受噪声影响
    // 这里简化为使用幅度的对数作为尖锐度的近似
    double sharpnessScore = (std::log10(anchorPeak.magnitude + 1) + std::log10(targetPeak.magnitude + 1)) / 2.0;
    score += sharpnessScore * 0.05;
    
    return score;
}

// 计算三帧峰值组合的评分
double HashComputer::computeTripleFrameCombinationScore(
    const Peak& anchorPeak,
    const Peak& targetPeak1,
    const Peak& targetPeak2) {
    
    double score = 0.0;
    
    // 1. 幅度评分 (40% 权重) - 优先选择幅度较大的峰值组合
    // 使用几何平均值来平衡三个峰值的幅度
    double magnitudeScore = std::pow(anchorPeak.magnitude * targetPeak1.magnitude * targetPeak2.magnitude, 1.0/3.0);
    score += magnitudeScore * 0.4;
    
    // 2. 频率差稳定性评分 (30% 权重) - 优先选择频率差适中的组合
    int32_t freqDelta1 = std::abs(static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak1.frequency));
    int32_t freqDelta2 = std::abs(static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak2.frequency));
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    
    // 计算两个频率差在有效范围内的归一化位置 [0, 1]
    double freqDelta1Normalized = static_cast<double>(freqDelta1 - signatureConfig.minFreqDelta) / 
                                  (signatureConfig.maxFreqDelta - signatureConfig.minFreqDelta);
    double freqDelta2Normalized = static_cast<double>(freqDelta2 - signatureConfig.minFreqDelta) / 
                                  (signatureConfig.maxFreqDelta - signatureConfig.minFreqDelta);
    
    // 使用倒置的二次函数，使中等频率差获得更高分数
    double freqDelta1Score = 1.0 - 4.0 * std::pow(freqDelta1Normalized - 0.5, 2);
    double freqDelta2Score = 1.0 - 4.0 * std::pow(freqDelta2Normalized - 0.5, 2);
    freqDelta1Score = std::max(0.0, freqDelta1Score);
    freqDelta2Score = std::max(0.0, freqDelta2Score);
    
    // 取两个频率差评分的平均值
    double avgFreqDeltaScore = (freqDelta1Score + freqDelta2Score) / 2.0;
    score += avgFreqDeltaScore * 25.0 * 0.3; // 乘以25调整分数范围
    
    // 3. 时间差稳定性评分 (20% 权重) - 优先选择时间差较小的组合
    double timeDelta1 = std::abs(anchorPeak.timestamp - targetPeak1.timestamp);
    double timeDelta2 = std::abs(anchorPeak.timestamp - targetPeak2.timestamp);
    double timeDelta1Normalized = timeDelta1 / signatureConfig.maxTimeDelta; // [0, 1]
    double timeDelta2Normalized = timeDelta2 / signatureConfig.maxTimeDelta; // [0, 1]
    
    // 时间差越小，分数越高
    double timeDelta1Score = (1.0 - timeDelta1Normalized) * 10.0;
    double timeDelta2Score = (1.0 - timeDelta2Normalized) * 10.0;
    double avgTimeDeltaScore = (timeDelta1Score + timeDelta2Score) / 2.0;
    score += avgTimeDeltaScore * 0.2;
    
    // 4. 频率位置评分 (7% 权重) - 优先选择中频段的峰值
    double avgFreq = (anchorPeak.frequency + targetPeak1.frequency + targetPeak2.frequency) / 3.0;
    double freqPositionScore = 0.0;
    
    if (avgFreq >= 1000 && avgFreq <= 3000) {
        freqPositionScore = 10.0;
    } else if (avgFreq >= 500 && avgFreq <= 4000) {
        freqPositionScore = 7.0;
    } else {
        freqPositionScore = 3.0;
    }
    score += freqPositionScore * 0.07;
    
    // 5. 峰值尖锐度评分 (3% 权重) - 优先选择更尖锐的峰值
    double sharpnessScore = (std::log10(anchorPeak.magnitude + 1) + 
                           std::log10(targetPeak1.magnitude + 1) + 
                           std::log10(targetPeak2.magnitude + 1)) / 3.0;
    score += sharpnessScore * 0.03;
    
    return score;
}

} // namespace afp
