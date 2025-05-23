#include "hash_computer.h"
#include <iostream>
#include <algorithm>
#include <cmath>

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
    
    // 确保我们有至少3帧
    if (longFrames.size() < 3) {
        std::cout << "[DEBUG-指纹生成] HashComputer: 帧历史不足3帧，无法生成指纹" << std::endl;
        return result;
    }

    // 获取指纹生成配置
    const auto& signatureConfig = config_->getSignatureGenerationConfig();
    
    // 使用滑动窗口方式处理所有可能的三帧组合
    size_t totalAcceptedCombinations = 0;
    
    // 计算可能的滑动窗口数量
    size_t numWindows = longFrames.size() - 2; // 至少需要3帧一组
    
    std::cout << "[DEBUG-指纹生成] HashComputer: 发现" << numWindows 
              << "个可能的三帧窗口组合，开始处理" << std::endl;
    
    // 对每个可能的三帧窗口进行处理
    for (size_t windowStart = 0; windowStart < numWindows; windowStart++) {
        // 获取当前窗口的三帧
        const Frame& frame1 = longFrames[windowStart];     // 窗口中最旧的帧
        const Frame& frame2 = longFrames[windowStart + 1]; // 窗口中间帧
        const Frame& frame3 = longFrames[windowStart + 2]; // 窗口中最新的帧
        
        std::cout << "[DEBUG-指纹生成] HashComputer: 处理第" << (windowStart + 1) << "/"
                  << numWindows << "个窗口，帧时间戳: " << frame1.timestamp << "s, "
                  << frame2.timestamp << "s, " << frame3.timestamp << "s" << std::endl;
        
        // 统计不同原因的过滤数量
        size_t totalPossibleCombinations = 0;
        size_t filteredByFreqDelta1_min = 0;
        size_t filteredByFreqDelta1_max = 0;
        size_t filteredByTimeDelta1 = 0;
        size_t filteredByFreqDelta2 = 0;
        size_t filteredByTimeDelta2 = 0;
        size_t filteredByFreqDeltaSimilarity = 0;
        size_t acceptedCombinations = 0;
        
        // 潜在组合总数
        size_t theoreticalCombinations = frame1.peaks.size() * frame2.peaks.size() * frame3.peaks.size();
        
        // 跳过包含空帧的窗口
        if (frame1.peaks.empty() || frame2.peaks.empty() || frame3.peaks.empty()) {
            std::cout << "[警告-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                      << "存在空帧，跳过此窗口" << std::endl;
            continue;
        }
        
        std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                  << "理论可能的峰值组合数: " << theoreticalCombinations << " (帧1:"
                  << frame1.peaks.size() << "峰值, 帧2:" << frame2.peaks.size() 
                  << "峰值, 帧3:" << frame3.peaks.size() << "峰值)" << std::endl;
        
        // 从中间帧选择锚点峰值
        for (const auto& anchorPeak : frame2.peaks) {
            // 从第一帧（最旧）和第三帧（最新）中选择目标峰值
            for (const auto& targetPeak1 : frame1.peaks) {
                // 计算所有可能的组合数
                totalPossibleCombinations += frame3.peaks.size();
                
                // 计算第一个频率差并检查是否在有效范围内
                int32_t freqDelta1 = static_cast<int32_t>(anchorPeak.frequency - targetPeak1.frequency);
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
                    int32_t freqDelta2 = static_cast<int32_t>(targetPeak2.frequency - anchorPeak.frequency);
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
                    
                    // 计算三帧组合哈希值，使用峰值的实际时间戳，而不是帧的时间戳
                    uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);
                    
                    // 创建签名点
                    SignaturePoint signaturePoint;
                    signaturePoint.hash = hash;
                    signaturePoint.timestamp = anchorPeak.timestamp; // 使用锚点峰值的精确时间戳
                    signaturePoint.frequency = anchorPeak.frequency;
                    signaturePoint.amplitude = static_cast<uint32_t>(anchorPeak.magnitude * 1000);
                    
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
            }
        }
        
        totalAcceptedCombinations += acceptedCombinations;
        
        // 输出窗口过滤统计信息
        if (totalPossibleCombinations > 0) {
            std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                      << "总可能组合: " << totalPossibleCombinations 
                      << ", 接受: " << acceptedCombinations 
                      << " (" << (acceptedCombinations * 100.0 / totalPossibleCombinations) << "%)" << std::endl;
            
            // 详细的过滤统计可以在调试或分析时显示
            if (acceptedCombinations > 0) {
                std::cout << "[DEBUG-指纹生成] HashComputer: 窗口" << (windowStart + 1) 
                          << "过滤分布 - FreqDelta1_min: " << filteredByFreqDelta1_min << " (" << (filteredByFreqDelta1_min * 100.0 / totalPossibleCombinations) << "%)"
                          << ", FreqDelta1_max: " << filteredByFreqDelta1_max << " (" << (filteredByFreqDelta1_max * 100.0 / totalPossibleCombinations) << "%)"
                          << ", TimeDelta1: " << filteredByTimeDelta1 << " (" << (filteredByTimeDelta1 * 100.0 / totalPossibleCombinations) << "%)"
                          << ", FreqDelta2: " << filteredByFreqDelta2 << " (" << (filteredByFreqDelta2 * 100.0 / totalPossibleCombinations) << "%)"
                          << ", TimeDelta2: " << filteredByTimeDelta2 << " (" << (filteredByTimeDelta2 * 100.0 / totalPossibleCombinations) << "%)"
                          << ", FreqDeltaSimilarity: " << filteredByFreqDeltaSimilarity << " (" << (filteredByFreqDeltaSimilarity * 100.0 / totalPossibleCombinations) << "%)"
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
    
    return result;
}

// 计算三帧组合哈希值
uint32_t HashComputer::computeTripleFrameHash(
    const Peak& anchorPeak,
    const Peak& targetPeak1,
    const Peak& targetPeak2) {
    
    // 计算频率差
    int32_t freqDelta1 = static_cast<int32_t>(anchorPeak.frequency - targetPeak1.frequency);
    int32_t freqDelta2 = static_cast<int32_t>(targetPeak2.frequency - anchorPeak.frequency);
    
    // 计算时间差（毫秒）
    int32_t timeDelta1 = static_cast<int32_t>((anchorPeak.timestamp - targetPeak1.timestamp) * 1000);
    int32_t timeDelta2 = static_cast<int32_t>((targetPeak2.timestamp - anchorPeak.timestamp) * 1000);
    
    // 根据长帧时长调整量化步长
    // 对于0.1秒的帧，使用10ms量化，更长的帧使用更大的量化步长
    int32_t quantizationStep = static_cast<int32_t>(config_->getSignatureGenerationConfig().frameDuration * 100);
    
    // 约束时间差范围，调整为适应当前帧时长的范围
    // 保持约束范围在[-32,31]，但量化步长随帧时长变化
    int32_t timeDelta1Constrained = std::max(-32, std::min(31, timeDelta1 / quantizationStep));
    int32_t timeDelta2Constrained = std::max(-32, std::min(31, timeDelta2 / quantizationStep));
    
    // 调整为无符号表示用于位操作
    uint32_t timeDelta1Unsigned = static_cast<uint32_t>(timeDelta1Constrained + 32) & 0x3F; // 6位
    uint32_t timeDelta2Unsigned = static_cast<uint32_t>(timeDelta2Constrained + 32) & 0x3F; // 6位
    
    // 处理频率差，除以2增加鲁棒性，然后限制在10位表示范围内
    uint32_t freqDelta1Mapped = static_cast<uint32_t>(std::abs(freqDelta1 / 2) & 0x1FF); // 9位幅度
    uint32_t freqDelta2Mapped = static_cast<uint32_t>(std::abs(freqDelta2 / 2) & 0x1FF); // 9位幅度
    
    // 保留符号位（第10位）
    if (freqDelta1 < 0) freqDelta1Mapped |= 0x200;
    if (freqDelta2 < 0) freqDelta2Mapped |= 0x200;
    
    // 包含幅度信息 - 使用对数比例和分桶策略增强鲁棒性
    // 计算对数幅度差，然后映射到更稳定的范围内
    float logAmpDiff1 = std::log10(anchorPeak.magnitude + 0.01f) - std::log10(targetPeak1.magnitude + 0.01f);
    float logAmpDiff2 = std::log10(anchorPeak.magnitude + 0.01f) - std::log10(targetPeak2.magnitude + 0.01f);
    
    // 将对数差异映射到0-7的范围，增加一位用于表示符号，提供更细致的幅度差异表示
    // 使用较大的分桶(0.5)减少对微小幅度变化的敏感度
    uint8_t ampFactor1 = static_cast<uint8_t>(std::min(7, static_cast<int>((std::abs(logAmpDiff1) * 2) + 0.5f)));
    uint8_t ampFactor2 = static_cast<uint8_t>(std::min(7, static_cast<int>((std::abs(logAmpDiff2) * 2) + 0.5f)));
    
    // 加入符号位
    if (logAmpDiff1 < 0) ampFactor1 |= 0x8;
    if (logAmpDiff2 < 0) ampFactor2 |= 0x8;
    
    // 创建第一组异或组合（频率差1和时间差1），加入幅度因子
    // 使用异或运算结合频率、时间和幅度信息，增加区分度
    uint32_t combo1 = (freqDelta1Mapped & 0x3FF) ^ 
                     ((timeDelta1Unsigned & 0x03) << 8) ^ // 时间差低2位移到高位
                     ((timeDelta1Unsigned & 0x3C) << 2) ^ // 时间差高4位调整位置
                     ((ampFactor1 & 0xF) << 6);           // 加入4位幅度因子
    
    // 创建第二组异或组合（频率差2和时间差2），加入幅度因子
    uint32_t combo2 = (freqDelta2Mapped & 0x3FF) ^ 
                     ((timeDelta2Unsigned & 0x03) << 8) ^ // 时间差低2位移到高位
                     ((timeDelta2Unsigned & 0x3C) << 2) ^ // 时间差高4位调整位置
                     ((ampFactor2 & 0xF) << 6);           // 加入4位幅度因子
    
    // 确保组合结果不超过10位
    combo1 &= 0x3FF;
    combo2 &= 0x3FF;
    
    // 最终的哈希组合
    uint32_t hash = ((anchorPeak.frequency & 0xFFF) << 20) | // 锚点频率 (12位) - 位置20-31
                   (combo1 << 10) |                         // 第一组组合 (10位) - 位置10-19
                   combo2;                                  // 第二组组合 (10位) - 位置0-9
    
    return hash;
}

} // namespace afp
