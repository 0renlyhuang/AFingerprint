#include "hash_computation_phase.h"
#include <iostream>
#include <algorithm>
#include "base/scored_triple_frame_combination.h"

namespace afp {

HashComputationPhase::HashComputationPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx)
    , symmetric_frame_range_(ctx_->config->getSignatureGenerationConfig().symmetricFrameRange)
    , signature_generation_config_(ctx_->config->getSignatureGenerationConfig()) {

    const auto ring_buf_size = symmetric_frame_range_ * 2 + 1;
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        frame_ring_buffers_[i] = std::make_unique<RingBuffer<Frame>>(ring_buf_size);
    }

    const auto max_signature_point_count_per_channel = ctx_->config->getSignatureGenerationConfig().maxTripleFrameCombinations * symmetric_frame_range_;
    existing_triple_frame_combinations_.reserve(max_signature_point_count_per_channel * ctx_->channel_count);
    signature_points_.reserve(max_signature_point_count_per_channel * ctx_->channel_count);

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] HashComputationPhase 初始化:" << std::endl;
    std::cout << "  通道数: " << ctx_->channel_count << std::endl;
    std::cout << "  对称帧范围: " << symmetric_frame_range_ << std::endl;
    std::cout << "  环形缓冲区大小: " << ring_buf_size << std::endl;
    std::cout << "  每通道最大指纹点数: " << max_signature_point_count_per_channel << std::endl;
    std::cout << "  最大三帧组合数: " << signature_generation_config_.maxTripleFrameCombinations << std::endl;
    std::cout << "  频率差范围: [" << signature_generation_config_.minFreqDelta 
              << ", " << signature_generation_config_.maxFreqDelta << "]Hz" << std::endl;
    std::cout << "  时间差限制: " << signature_generation_config_.maxTimeDelta << "s" << std::endl;
    std::cout << "  最小三帧评分: " << signature_generation_config_.minTripleFrameScore << std::endl;
#endif
}

void HashComputationPhase::handleFrame(ChannelArray<std::vector<Frame>>& channel_long_frames) {
#ifdef ENABLED_DIAGNOSE
    size_t total_frames = 0;
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        total_frames += channel_long_frames[i].size();
    }
    std::cout << "[DIAGNOSE-哈希计算] 开始处理长帧: 总长帧数=" << total_frames << std::endl;
    
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        std::cout << "  通道" << i << ": 输入" << channel_long_frames[i].size() << "个长帧, 当前环形缓冲区" << frame_ring_buffers_[i]->size() << "个长帧" << std::endl;
        for (size_t j = 0; j < channel_long_frames[i].size(); j++) {
            const auto& frame = channel_long_frames[i][j];
            std::cout << "    长帧" << j << ": 时间戳=" << frame.timestamp 
                      << "s, 峰值数=" << frame.peaks.size() << std::endl;
        }
    }
#endif

    for (size_t i = 0; i < ctx_->channel_count; i++) {
        auto& long_frames = channel_long_frames[i];

        for (auto& frame : long_frames) {
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-哈希计算] 通道" << i << "添加长帧到环形缓冲区: 时间戳=" 
                      << frame.timestamp << "s, 峰值数=" << frame.peaks.size() 
                      << ", 历史缓冲区大小=" << frame_ring_buffers_[i]->size() 
                      << "/" << frame_ring_buffers_[i]->capacity() << std::endl;
#endif
            
            frame_ring_buffers_[i]->push(frame);

            if (frame_ring_buffers_[i]->full()) {
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-哈希计算] 通道" << i << "环形缓冲区已满，开始消费帧" << std::endl;
#endif
                consumeFrame(i);
                frame_ring_buffers_[i]->pop();
            }
        }
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] 本轮生成指纹点数: " << signature_points_.size() << std::endl;
    std::cout << "  已存在组合数: " << existing_triple_frame_combinations_.size() << std::endl;
#endif

    if (signature_points_.size() > 0) {
        ctx_->on_signature_points_generated(signature_points_);
        
        existing_triple_frame_combinations_.clear();
        signature_points_.clear();
    }
}

void HashComputationPhase::consumeFrame(size_t channel) {

    auto& ring_buffer = frame_ring_buffers_[channel];
    const auto anchorIndex = symmetric_frame_range_;

    // 添加缺失的变量声明
    size_t totalAcceptedCombinations = 0;

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "开始消费帧，锚点索引=" << anchorIndex << std::endl;
    
    // 输出环形缓冲区中所有帧的信息
    for (size_t i = 0; i < ring_buffer->size(); i++) {
        const Frame& frame = (*ring_buffer)[i];
        std::cout << "  索引" << i << ": 时间戳=" << frame.timestamp 
                  << "s, 峰值数=" << frame.peaks.size() << std::endl;
    }
#endif

    // 生成对称的三帧组合：从(x-n, x, x+n)到(x-1, x, x+1)
    for (size_t distance = 1; distance <= symmetric_frame_range_; distance++) {
    size_t frame1Index = anchorIndex - distance;  // 左侧帧
    size_t frame2Index = anchorIndex;             // 锚点帧
    size_t frame3Index = anchorIndex + distance;  // 右侧帧

    const Frame& frame1 = (*ring_buffer)[frame1Index];
    const Frame& frame2 = (*ring_buffer)[frame2Index]; 
    const Frame& frame3 = (*ring_buffer)[frame3Index];

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "处理距离=" << distance 
              << "的三帧组合，帧索引[" << frame1Index << "," << frame2Index << "," << frame3Index 
              << "]，时间戳[" << frame1.timestamp << "s," << frame2.timestamp << "s," 
              << frame3.timestamp << "s]" << std::endl;
#endif

    // 跳过包含空帧的窗口
    if (frame1.peaks.empty() || frame2.peaks.empty() || frame3.peaks.empty()) {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "锚点" << anchorIndex  << "，距离" << distance << "，"
                  << "存在空帧，跳过此窗口" << std::endl;
#endif
        continue;
    }

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

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "锚点" << anchorIndex  << "，距离" << distance << "，"
              << "理论可能的峰值组合数: " << theoreticalCombinations << " (帧1:"
              << frame1.peaks.size() << "峰值, 帧2:" << frame2.peaks.size() 
              << "峰值, 帧3:" << frame3.peaks.size() << "峰值)" << std::endl;
#endif

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
            if (std::abs(freqDelta1) < signature_generation_config_.minFreqDelta) {
                filteredByFreqDelta1_min += frame3.peaks.size();
                continue; // 跳过频率差太小
            }
            if (std::abs(freqDelta1) > signature_generation_config_.maxFreqDelta) {
                filteredByFreqDelta1_max += frame3.peaks.size();
                continue; // 跳过频率差太大
            }
            
            // 检查时间差是否在有效范围内
            double timeDelta1 = anchorPeak.timestamp - targetPeak1.timestamp;
            if (std::abs(timeDelta1) > signature_generation_config_.maxTimeDelta) {
                filteredByTimeDelta1 += frame3.peaks.size();
                continue; // 跳过时间差太大的配对
            }

            for (const auto& targetPeak2 : frame3.peaks) {
                // 计算第二个频率差并检查是否在有效范围内
                int32_t freqDelta2 = static_cast<int32_t>(targetPeak2.frequency) - static_cast<int32_t>(anchorPeak.frequency);
                if (std::abs(freqDelta2) < signature_generation_config_.minFreqDelta || 
                    std::abs(freqDelta2) > signature_generation_config_.maxFreqDelta) {
                    filteredByFreqDelta2++;
                    continue; // 跳过频率差太小或太大的配对
                }
                
                // 检查时间差是否在有效范围内
                double timeDelta2 = targetPeak2.timestamp - anchorPeak.timestamp;
                if (std::abs(timeDelta2) > signature_generation_config_.maxTimeDelta) {
                    filteredByTimeDelta2++;
                    continue; // 跳过时间差太大的配对
                }
                
                // 确保频率差之间有足够的差异，避免生成类似的哈希值
                if (std::abs(freqDelta1 - freqDelta2) < signature_generation_config_.minFreqDelta / 2) {
                    filteredByFreqDeltaSimilarity++;
                    continue; // 两个频率差太相似
                }
                
                // 计算评分
                double score = computeTripleFrameCombinationScore(anchorPeak, targetPeak1, targetPeak2);
                
                // 检查评分是否满足最低阈值
                if (score < signature_generation_config_.minTripleFrameScore) {
                    filteredByLowScore++;
                    continue; // 跳过评分过低的组合
                }
                
                // 计算三帧组合哈希值，使用峰值的实际时间戳，而不是帧的时间戳
                // uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);
                
                // 添加到有效组合列表
                ScoredTripleFrameCombination combination;
                combination.anchorPeak = &anchorPeak;
                combination.targetPeak1 = &targetPeak1;
                combination.targetPeak2 = &targetPeak2;
                combination.score = score;
                // combination.hash = hash;
                
                validCombinationsVec.push_back(combination);
                validCombinations++;
            }
        }
    }

    // TODO: 评估是排序好还是用堆好

    // 按评分排序，保留topN
    std::sort(validCombinationsVec.begin(), validCombinationsVec.end(), std::greater<ScoredTripleFrameCombination>());

    // 限制保留的组合数量
    size_t maxCombinations = std::min(validCombinationsVec.size(), signature_generation_config_.maxTripleFrameCombinations);
    acceptedCombinations = 0;

    // Todo: 按时间排序

    // 生成签名点
    for (size_t i = 0; i < maxCombinations; i++) {
        const auto& combination = validCombinationsVec[i];
        const auto& anchorPeak = *combination.anchorPeak;
        const auto& targetPeak1 = *combination.targetPeak1;
        const auto& targetPeak2 = *combination.targetPeak2;

        uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);

        // 创建签名点
        SignaturePoint signaturePoint;
        signaturePoint.hash = hash;
        signaturePoint.timestamp = anchorPeak.timestamp; // 使用锚点峰值的精确时间戳
        signaturePoint.frequency = anchorPeak.frequency;
        signaturePoint.amplitude = static_cast<uint32_t>(anchorPeak.magnitude * 1000);
        
        // Add to visualization data if enabled
        if (ctx_->visualization_config->collectVisualizationData_) {
            ctx_->visualization_config->visualizationData_.fingerprintPoints.emplace_back(
                signaturePoint.frequency, 
                signaturePoint.timestamp, 
                signaturePoint.hash
            );
        }

        std::pair<uint32_t, double> unique_key(signaturePoint.hash, signaturePoint.timestamp);
        if (existing_triple_frame_combinations_.find(unique_key) == existing_triple_frame_combinations_.end()) {
            existing_triple_frame_combinations_.insert(unique_key);
            signature_points_.push_back(signaturePoint);

#ifdef ENABLED_DIAGNOSE
            if (acceptedCombinations < 3) { // 只显示前几个
                std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "生成指纹点" << (acceptedCombinations + 1) 
                          << ": 哈希=" << std::hex << hash << std::dec 
                          << ", 时间=" << signaturePoint.timestamp << "s"
                          << ", 频率=" << signaturePoint.frequency << "Hz"
                          << ", 评分=" << combination.score << std::endl;
            }
#endif
        }

        acceptedCombinations++;
    }

    totalAcceptedCombinations += acceptedCombinations;

    // 输出窗口过滤统计信息
    if (totalPossibleCombinations > 0) {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "锚点" << anchorIndex  << "，距离" << distance << "，"
                  << "总可能组合: " << totalPossibleCombinations 
                  << ", 有效组合: " << validCombinations
                  << ", 接受: " << acceptedCombinations 
                  << " (" << (acceptedCombinations * 100.0 / totalPossibleCombinations) << "%)" << std::endl;
        
        // 详细的过滤统计
        if (validCombinations > 0) {
            std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "锚点" << anchorIndex  << "，距离" << distance << "，"
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
#endif
    } else {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "锚点" << anchorIndex  << "，距离" << distance << "，"
                  << "没有可能的峰值组合，无法生成指纹" << std::endl;
#endif
    }
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-哈希计算] 通道" << channel << "消费帧完成，总接受组合数: " 
              << totalAcceptedCombinations << ", 本次生成指纹点数: " 
              << signature_points_.size() << std::endl;
#endif
}




// 计算三帧峰值组合的评分
double HashComputationPhase::computeTripleFrameCombinationScore(
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
    
    // 计算两个频率差在有效范围内的归一化位置 [0, 1]
    double freqDelta1Normalized = static_cast<double>(freqDelta1 - signature_generation_config_.minFreqDelta) / 
                                  (signature_generation_config_.maxFreqDelta - signature_generation_config_.minFreqDelta);
    double freqDelta2Normalized = static_cast<double>(freqDelta2 - signature_generation_config_.minFreqDelta) / 
                                  (signature_generation_config_.maxFreqDelta - signature_generation_config_.minFreqDelta);
    
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
    double timeDelta1Normalized = timeDelta1 / signature_generation_config_.maxTimeDelta; // [0, 1]
    double timeDelta2Normalized = timeDelta2 / signature_generation_config_.maxTimeDelta; // [0, 1]
    
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


// 计算三帧组合哈希值
uint32_t HashComputationPhase::computeTripleFrameHash(
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
    //        异或区域[14:11] anchor-target1的时间差 (4位) - 位置11-14, 时间差,最大是0.08*3=0.24s, 除以 0.2s
    //     将符号信息和时间差信息异或叠加到频率差信息的[14:10]上，得到combo1
    // [9:0]  combo2 (10位)         - 位置0-9
    //     [9:0] anchor-target2 频率差绝对值 (10位)     - 频率差绝对值 除以 4Hz
    //        异或区域[0:0] anchor-target2的符号位(1位置) - 位置0，如果anchor-target2是负数，则该位置为1，否则为0
    //        异或区域[4:1] anchor-target2的时间差 (4位) - 位置1-4, 时间差, 最大是0.08*3=0.24s, 除以 0.2s
    //     将符号信息和时间差信息异或叠加到频率差信息的[4:0]上，得到combo2
    // 因此需要12+10+10=32位
    
    // 1. 计算锚点频率 (12位，位置20-31)
    uint32_t anchorFreqQuantized = (anchorPeak.frequency / 4) & 0xFFF; // 除以4Hz，限制为12位
    
    // 2. 计算combo1 (anchor-target1的组合)
    int32_t freqDelta1 = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak1.frequency);
    uint32_t freqDelta1Abs = (static_cast<uint32_t>(std::abs(freqDelta1)) / 4) & 0x3FF; // 除以4Hz，限制为10位
    uint32_t freqDelta1Sign = (freqDelta1 < 0) ? 1 : 0; // 符号位
    
    double timeDelta1 = anchorPeak.timestamp - targetPeak1.timestamp;
    uint32_t timeDelta1Quantized = static_cast<uint32_t>(std::max(0.0, std::min(7.0, std::abs(timeDelta1) / 0.09))) & 0x7; // 除以0.3s，限制为3位
    
    // 构建combo1：将符号位和时间差信息异或到频率差信息的低5位[4:0]
    uint32_t combo1 = freqDelta1Abs;
    uint32_t timeSignCombo1 = (freqDelta1Sign) | (timeDelta1Quantized << 1); // 符号位(1位) + 时间差(3位) = 4位
    combo1 ^= timeSignCombo1; // 异或到频率差的低5位
    combo1 &= 0x3FF; // 确保只有10位
    
    // 3. 计算combo2 (anchor-target2的组合)
    int32_t freqDelta2 = static_cast<int32_t>(anchorPeak.frequency) - static_cast<int32_t>(targetPeak2.frequency);
    uint32_t freqDelta2Abs = (static_cast<uint32_t>(std::abs(freqDelta2)) / 47) & 0x3F; // 除以47Hz，限制为6位
    uint32_t freqDelta2Sign = (freqDelta2 < 0) ? 1 : 0; // 符号位
    
    double timeDelta2 = anchorPeak.timestamp - targetPeak2.timestamp;
    uint32_t timeDelta2Quantized = static_cast<uint32_t>(std::max(0.0, std::min(7.0, std::abs(timeDelta2) / 0.06))) & 0x7; // 除以0.3s，限制为3位
    
    // 构建combo2：将符号位和时间差信息异或到频率差信息的低4位[3:0]
    uint32_t combo2 = (freqDelta2Abs << 4) | (timeDelta2Quantized << 1) | freqDelta2Sign;
    
    // 4. 组合32位哈希值
    // [31:20] 锚点频率(12位) | [19:10] combo1(10位) | [9:0] combo2(10位)
    uint32_t hash = (anchorFreqQuantized << 20) |  // 锚点频率 (12位) - 位置20-31
                   (combo1 << 10) |               // combo1 (10位) - 位置10-19
                   combo2;                        // combo2 (10位) - 位置0-9
    
    // std::cout << "debugging timeDelta1: " << timeDelta1 << " timeDelta2: " << timeDelta2 << std::endl;

    return hash;
}


}