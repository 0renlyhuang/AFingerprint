#include "signature/signature_generator.h"
#include "fft/fft_interface.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <unordered_set>

/**
 * 签名生成器实现
 * 
 * 峰值检测的核心原理:
 * 1. 在频率维度上，使用localMaxRange参数确定局部最大值的检测范围
 *    - 需要比周围localMaxRange个频率bin都要大才被认为是频率维度上的峰值
 * 
 * 2. 在时间维度上，使用timeMaxRange参数确定局部最大值的检测范围
 *    - 需要比前后timeMaxRange个时间帧的对应频率bin都要大才被认为是时间维度上的峰值
 *    - 当timeMaxRange = 1时，相当于只与相邻帧比较(原始实现)
 *    - 当timeMaxRange > 1时，会与更多前后帧比较，产生更具有时间一致性的峰值
 * 
 * 配置示例:
 * - 移动端: timeMaxRange = 1，仅与相邻帧比较，性能更好
 * - 台式机: timeMaxRange = 2，与前后2帧比较，平衡性能和准确度
 * - 服务器: timeMaxRange = 3，与前后3帧比较，获得更高的准确度
 */

namespace afp {

// 为std::pair<uint32_t, double>提供哈希函数
struct PairHash {
    size_t operator()(const std::pair<uint32_t, double>& p) const {
        // 组合两个哈希值
        return std::hash<uint32_t>{}(p.first) ^ 
               (std::hash<double>{}(p.second) << 1);
    }
};

SignatureGenerator::SignatureGenerator(std::shared_ptr<IPerformanceConfig> config)
    : config_(config)
    , fftSize_(config->getFFTConfig().fftSize)
    , hopSize_(config->getFFTConfig().hopSize)  // 从配置中获取帧移大小
    , frameDuration_(config->getSignatureGenerationConfig().frameDuration)  // 从配置中获取长帧时长
    , samplesPerFrame_(0)  // 将在init中设置
    , samplesPerShortFrame_(0)  // 将在init中设置
    , shortFramesPerLongFrame_(0)  // 将在init中设置
    , fft_(FFTFactory::create(fftSize_)) {
    
    // 初始化汉宁窗
    window_.resize(fftSize_);
    for (size_t i = 0; i < fftSize_; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize_ - 1)));
    }
    
    // 初始化缓冲区
    buffer_.resize(fftSize_);
    fftBuffer_.resize(fftSize_);
}

SignatureGenerator::~SignatureGenerator() = default;

bool SignatureGenerator::init(const PCMFormat& format) {
    format_ = format;
    sampleRate_ = format.sampleRate();
    reader_ = std::make_unique<PCMReader>(format);
    
    // 计算每个长帧需要的样本数，使用配置的frameDuration_
    samplesPerFrame_ = static_cast<size_t>(frameDuration_ * sampleRate_);
    
    // 使用fftSize_作为短帧的样本数量，而不是基于时间计算
    samplesPerShortFrame_ = fftSize_;
    
    // 检查hopSize是否合理，如果未设置或者大于窗口大小，则默认为窗口大小的一半
    if (hopSize_ == 0 || hopSize_ >= fftSize_) {
        hopSize_ = fftSize_ / 2;
        std::cout << "[Debug] hopSize设置不合理，已重置为fftSize的一半: " << hopSize_ << std::endl;
    }
    
    // 获取峰值检测配置
    const auto& peakConfig = config_->getPeakDetectionConfig();
    
    // 计算每个短帧的持续时间（秒）
    double shortFrameDuration = static_cast<double>(hopSize_) / sampleRate_;
    
    // 重新计算每个长帧包含的短帧数，使用配置的frameDuration_和hopSize决定的短帧时长
    shortFramesPerLongFrame_ = static_cast<size_t>(std::ceil(frameDuration_ / shortFrameDuration));
    
    // 清空所有缓冲区和历史记录
    frameHistoryMap_.clear();
    channelBuffers_.clear();
    fftResultsMap_.clear();
    
    // Reset visualization data
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
        visualizationData_.duration = 0.0;  // Will be updated during processing
    }
    
    std::cout << "[Debug] 初始化完成: 采样率=" << sampleRate_ 
              << "Hz, 长帧时长=" << frameDuration_
              << "s, 每长帧样本数=" << samplesPerFrame_
              << ", 每短帧样本数=" << samplesPerShortFrame_
              << ", 帧移大小=" << hopSize_
              << ", 帧重叠率=" << (1.0 - static_cast<double>(hopSize_) / fftSize_) * 100.0 << "%"
              << ", 每短帧持续时间=" << shortFrameDuration
              << "s, 每长帧包含短帧数=" << shortFramesPerLongFrame_
              << ", FFT大小=" << fftSize_ 
              << ", 频率维度最大值范围=" << peakConfig.localMaxRange
              << ", 时间维度最大值范围=" << peakConfig.timeMaxRange << std::endl;
              
    return true;
}

// Save visualization data to file
bool SignatureGenerator::saveVisualization(const std::string& filename) const {
    if (!collectVisualizationData_) {
        std::cerr << "Visualization data collection is not enabled" << std::endl;
        return false;
    }
    
    // Save data to JSON (no Python script generation)
    return Visualizer::saveVisualization(visualizationData_, filename);
}

// 流式处理音频数据
bool SignatureGenerator::appendStreamBuffer(const void* buffer, 
                                         size_t bufferSize,
                                         double startTimestamp) {
    if (!buffer || bufferSize == 0) {
        return false;
    }

    // 记录处理前的签名数量，用于返回是否生成了新的签名
    size_t initialSignatureCount = signatures_.size();
    
    // 临时缓冲区，用于存储PCM读取器处理的数据
    std::map<uint32_t, std::vector<float>> tempChannelBuffers;
    
    // 处理所有通道的数据
    reader_->process(buffer, bufferSize, [&](float sample, uint32_t channel) {
        tempChannelBuffers[channel].push_back(sample);
    });
    
    if (tempChannelBuffers.empty()) {
        return false;
    }
    
    std::cout << "处理音频数据: " << bufferSize / format_.frameSize() << " 样本, "
              << tempChannelBuffers.size() << " 个通道, 每帧样本数: " 
              << samplesPerFrame_ << std::endl;
    
    // 处理每个通道的数据
    for (auto& [channel, samples] : tempChannelBuffers) {
        // 确保当前通道有缓冲区
        if (channelBuffers_.find(channel) == channelBuffers_.end()) {
            channelBuffers_[channel].samples.resize(samplesPerShortFrame_, 0.0f);
            channelBuffers_[channel].consumed = 0;
            channelBuffers_[channel].timestamp = startTimestamp;
            
            // 初始化该通道的FFT结果缓冲区
            fftResultsMap_[channel].clear();
        }
        
        ChannelBuffer& channelBuffer = channelBuffers_[channel];
        
        // 将新样本添加到通道缓冲区
        size_t sampleIndex = 0;
        while (sampleIndex < samples.size()) {
            // 填充通道缓冲区直到满一个短帧
            size_t remainingSpace = samplesPerShortFrame_ - channelBuffer.consumed;
            size_t samplesToAdd = std::min(remainingSpace, samples.size() - sampleIndex);
            
            // 复制样本到缓冲区
            std::memcpy(channelBuffer.samples.data() + channelBuffer.consumed, 
                      samples.data() + sampleIndex, 
                      samplesToAdd * sizeof(float));
            
            channelBuffer.consumed += samplesToAdd;
            sampleIndex += samplesToAdd;
            
            // 如果缓冲区已满足一个短帧的数据量，处理短帧
            if (channelBuffer.consumed == samplesPerShortFrame_) {
                // 处理当前短帧
                processShortFrame(channelBuffer.samples.data(), channel, channelBuffer.timestamp);
                
                // 通过帧移实现重叠
                // 计算要保留的样本数（帧重叠部分）
                size_t samplesToKeep = samplesPerShortFrame_ - hopSize_;
                
                // 将数据后移到缓冲区前部，实现重叠
                if (samplesToKeep > 0) {
                    std::memmove(channelBuffer.samples.data(), 
                              channelBuffer.samples.data() + hopSize_, 
                              samplesToKeep * sizeof(float));
                }
                
                // 更新已消费样本数
                channelBuffer.consumed = samplesToKeep;
                
                // 更新时间戳为下一个短帧的开始时间，基于帧移大小
                channelBuffer.timestamp += static_cast<double>(hopSize_) / sampleRate_;
                
                // Update visualization duration if needed
                if (collectVisualizationData_ && channelBuffer.timestamp > visualizationData_.duration) {
                    visualizationData_.duration = channelBuffer.timestamp;
                }
                
                // 检查是否积累了足够的短帧FFT结果来处理一个长帧
                if (fftResultsMap_[channel].size() >= shortFramesPerLongFrame_) {
                    // 处理长帧（从短帧FFT结果中提取峰值）
                    processLongFrame(channel, fftResultsMap_[channel].front().timestamp);
                    
                    // 移除已处理的短帧FFT结果
                    fftResultsMap_[channel].erase(fftResultsMap_[channel].begin(), 
                                                fftResultsMap_[channel].begin() + shortFramesPerLongFrame_);
                }
            }
        }
    }
    
    // 对signatures_根据hash+timestamp去重，保持顺序不变
    {
        if (signatures_.size() > initialSignatureCount) {
            // 使用unordered_set作为查询结构以获得O(1)的查找性能
            std::unordered_set<std::pair<uint32_t, double>, PairHash> uniquePairs;
            
            // 创建一个去重后的临时向量
            std::vector<SignaturePoint> uniqueSignatures;
            uniqueSignatures.reserve(signatures_.size()); // 预分配内存避免多次重新分配
            
            // 从initialSignatureCount开始，只对新增加的签名进行去重
            for (size_t i = initialSignatureCount; i < signatures_.size(); ++i) {
                const auto& signature = signatures_[i];
                std::pair<uint32_t, double> key(signature.hash, signature.timestamp);
                
                // 如果这个(hash, timestamp)对尚未出现过，则添加到结果中
                if (uniquePairs.insert(key).second) {
                    uniqueSignatures.push_back(signature);
                }
            }
            
            // 替换原始signatures_向量中新增的部分
            if (uniqueSignatures.size() < signatures_.size() - initialSignatureCount) {
                signatures_.erase(signatures_.begin() + initialSignatureCount, signatures_.end());
                signatures_.insert(signatures_.end(), uniqueSignatures.begin(), uniqueSignatures.end());
                std::cout << "[Debug] 去重后减少了 " 
                          << (signatures_.size() - initialSignatureCount - uniqueSignatures.size())
                          << " 个重复指纹点" << std::endl;
            }
        }
    }
    
    size_t newSignaturesGenerated = signatures_.size() - initialSignatureCount;
    std::cout << "[Debug] 本次调用生成了 " << newSignaturesGenerated << " 个指纹点，总共有 " 
              << signatures_.size() << " 个指纹点" << std::endl;
    
    // 输出每个通道的缓冲区状态
    for (const auto& [channel, buffer] : channelBuffers_) {
        std::cout << "[Debug] 通道 " << channel << " 的缓冲区已消费 " 
                  << buffer.consumed << "/" << samplesPerShortFrame_ 
                  << " 样本，缓冲区时间戳: " << buffer.timestamp << std::endl;
    }
    
    // 输出每个通道的帧历史记录数量
    for (const auto& [channel, history] : frameHistoryMap_) {
        std::cout << "[Debug] 通道 " << channel << " 的帧历史包含 " << history.size() << " 帧" << std::endl;
    }

    return newSignaturesGenerated > 0 || signatures_.size() > initialSignatureCount;
}

// 处理短帧音频数据，执行FFT分析
void SignatureGenerator::processShortFrame(const float* frameBuffer, 
                                        uint32_t channel,
                                        double frameTimestamp) {
    static bool firstCall = true;
    
    // 初始化处理缓冲区
    buffer_.clear();
    buffer_.resize(fftSize_, 0.0f);
    
    // 将短帧数据复制到处理缓冲区
    std::memcpy(buffer_.data(), frameBuffer, fftSize_ * sizeof(float));
    
    // 添加调试信息
    if (firstCall && channel == 0) {  // 只为第一个通道添加调试信息
        AudioDebugger::checkAudioBuffer(frameBuffer, samplesPerShortFrame_, frameTimestamp, firstCall);
        AudioDebugger::checkCopiedBuffer(buffer_, 0, fftSize_);
        firstCall = false;
    }
    
    // 应用预加重滤波器以强调高频内容
    for (size_t i = 1; i < fftSize_; ++i) {
        buffer_[i] -= 0.95f * buffer_[i-1];
    }
    
    // 检查预加重后的数据（仅对第一个通道）
    if (channel == 0) {
        AudioDebugger::checkPreEmphasisBuffer(buffer_, 0, fftSize_);
    }
    
    // 应用窗函数
    std::vector<float> windowed(fftSize_);
    for (size_t i = 0; i < fftSize_; ++i) {
        windowed[i] = buffer_[i] * window_[i];
    }
    
    // 执行FFT
    if (!fft_->transform(windowed.data(), fftBuffer_.data())) {
        return;
    }
    
    // 创建FFT结果结构
    FFTResult fftResult;
    fftResult.magnitudes.resize(fftSize_ / 2);
    fftResult.frequencies.resize(fftSize_ / 2);
    fftResult.timestamp = frameTimestamp;
    
    float maxMagnitude = 0.0001f; // 避免除零
    
    // 计算幅度谱和频率
    for (size_t i = 0; i < fftSize_ / 2; ++i) {
        // 计算复数的模
        float magnitude = std::abs(fftBuffer_[i]);
        
        // 对数频谱
        fftResult.magnitudes[i] = magnitude > 0.00001f ? 20.0f * std::log10(magnitude) + 100.0f : 0;
        
        // 更新最大值
        if (fftResult.magnitudes[i] > maxMagnitude) {
            maxMagnitude = fftResult.magnitudes[i];
        }
        
        // 计算每个bin对应的频率
        fftResult.frequencies[i] = i * sampleRate_ / fftSize_;
    }
    
    // 正规化幅度谱
    for (size_t i = 0; i < fftSize_ / 2; ++i) {
        fftResult.magnitudes[i] /= maxMagnitude;
    }
    
    // 将FFT结果添加到通道的FFT结果缓冲区
    fftResultsMap_[channel].push_back(std::move(fftResult));
}

// 从短帧FFT结果缓冲区中提取峰值
std::vector<Peak> SignatureGenerator::extractPeaksFromFFTResults(
    const std::vector<FFTResult>& fftResults,
    double longFrameTimestamp) {
    
    std::vector<Peak> peaks;
    
    const auto& peakConfig = config_->getPeakDetectionConfig();
    
    // 检查是否有足够的短帧来进行时间维度上的峰值检测
    // 需要至少 2*timeMaxRange + 1 个帧，才能在时间维度上检测局部最大值
    // 例如：对于timeMaxRange=3，我们需要比较当前帧与前后各3帧，共需7帧
    if (fftResults.empty() || fftResults.size() < 2*peakConfig.timeMaxRange + 1) {
        return peaks;
    }
    
    // 创建一个候选峰值列表
    std::vector<Peak> candidatePeaks;
    
    // 在时频域上查找局部最大值
    // 跳过开始和结束的timeMaxRange个帧，以便在时间维度上能进行完整比较
    for (size_t frameIdx = peakConfig.timeMaxRange; 
         frameIdx < fftResults.size() - peakConfig.timeMaxRange; 
         ++frameIdx) {
        
        const auto& currentFrame = fftResults[frameIdx];
        
        // 跳过频谱边缘的频率bin，以便在频率维度上比较
        for (size_t freqIdx = peakConfig.localMaxRange; 
             freqIdx < fftSize_ / 2 - peakConfig.localMaxRange; 
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
            // 这可以有效地滤除临时或随机的噪声峰值，只保留在时间上持续存在的真实峰值
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
            if (collectVisualizationData_) {
                visualizationData_.allPeaks.emplace_back(peak.frequency, peak.timestamp);
            }
        }
    }
    
   // 限制每个长帧的峰值数量，同时保持原始相对顺序
   if (candidatePeaks.size() > peakConfig.maxPeaksPerFrame) {
       // 创建原始索引和幅度的对组
       std::vector<std::pair<size_t, float>> indexMagnitudePairs;
       indexMagnitudePairs.reserve(candidatePeaks.size());
       
       for (size_t i = 0; i < candidatePeaks.size(); ++i) {
           indexMagnitudePairs.emplace_back(i, candidatePeaks[i].magnitude);
       }
       
       // 部分排序，仅找出前maxPeaksPerFrame个最大元素的索引
       std::partial_sort(
           indexMagnitudePairs.begin(),
           indexMagnitudePairs.begin() + peakConfig.maxPeaksPerFrame,
           indexMagnitudePairs.end(),
           [](const auto& a, const auto& b) { return a.second > b.second; }
       );
       
       // 提取前maxPeaksPerFrame个最大幅度元素的索引
       std::vector<size_t> topIndices;
       topIndices.reserve(peakConfig.maxPeaksPerFrame);
       
       for (size_t i = 0; i < peakConfig.maxPeaksPerFrame; ++i) {
           topIndices.push_back(indexMagnitudePairs[i].first);
       }
       
       // 按原始索引排序，这样可以保持原始顺序
       std::sort(topIndices.begin(), topIndices.end());
       
       // 创建新的结果列表，保持原始顺序
       std::vector<Peak> filteredPeaks;
       filteredPeaks.reserve(peakConfig.maxPeaksPerFrame);
       
       for (size_t idx : topIndices) {
           filteredPeaks.push_back(candidatePeaks[idx]);
       }
       
       // 用筛选后的结果替换原始列表
       candidatePeaks = std::move(filteredPeaks);
   }
    
    return candidatePeaks;
}

// 处理长帧音频数据，从短帧FFT结果中提取峰值
void SignatureGenerator::processLongFrame(
                               uint32_t channel,
                               double frameTimestamp) {
    
    // 获取当前使用的峰值检测配置
    const auto& peakConfig = config_->getPeakDetectionConfig();
    
    // 确保有足够的短帧FFT结果用于提取峰值
    // 需要至少 2*timeMaxRange + 1 个短帧来检测时间维度上的峰值
    size_t minRequiredFrames = std::max(shortFramesPerLongFrame_, 2 * peakConfig.timeMaxRange + 1);
    if (fftResultsMap_[channel].size() < minRequiredFrames) {
        return;
    }
    
    // 获取该通道的所有短帧FFT结果
    std::vector<FFTResult> fftResults(fftResultsMap_[channel].begin(), 
                                      fftResultsMap_[channel].begin() + shortFramesPerLongFrame_);
    
    // 从短帧FFT结果中提取峰值 - 现在提取的是时频域上的真正局部最大值
    std::vector<Peak> currentPeaks = extractPeaksFromFFTResults(fftResults, frameTimestamp);
    
    if (!currentPeaks.empty()) {
        // 创建新帧并存储其峰值
        Frame newFrame;
        newFrame.peaks = currentPeaks;
        newFrame.timestamp = frameTimestamp; // 保留长帧时间戳用于标识帧
        
        // 为当前通道添加帧历史记录
        frameHistoryMap_[channel].push_back(newFrame);
        
        // 保持历史帧队列的大小不超过FRAME_BUFFER_SIZE
        while (frameHistoryMap_[channel].size() > FRAME_BUFFER_SIZE) {
            frameHistoryMap_[channel].pop_front();
        }
        
        // 当累积了足够数量的帧（3帧）时，生成跨帧指纹
        if (frameHistoryMap_[channel].size() == FRAME_BUFFER_SIZE) {
            std::vector<SignaturePoint> newPoints = generateTripleFrameSignatures(
                frameHistoryMap_[channel], frameTimestamp);
            
            // 添加生成的指纹
            signatures_.insert(signatures_.end(), newPoints.begin(), newPoints.end());
            
            // 调试输出
            // if (!newPoints.empty()) {
            //     std::cout << "[Debug] 从通道 " << channel << " 的三帧生成了 " << newPoints.size() 
            //               << " 个指纹点，当前时间戳: " << frameTimestamp 
            //               << "，最早峰值时间戳: " << currentPeaks.front().timestamp << std::endl;
            // }
        }
    }
}

// 从三帧峰值生成指纹
std::vector<SignaturePoint> SignatureGenerator::generateTripleFrameSignatures(
    const std::deque<Frame>& frameHistory,
    double currentTimestamp) {
    
    std::vector<SignaturePoint> signatures;
    
    // 确保我们有3帧
    if (frameHistory.size() < 3) {
        return signatures;
    }
    
    // 获取三个连续帧
    const Frame& frame1 = frameHistory[0]; // 最旧的帧
    const Frame& frame2 = frameHistory[1]; // 中间帧
    const Frame& frame3 = frameHistory[2]; // 最新的帧
    
    // 从中间帧选择锚点峰值
    for (const auto& anchorPeak : frame2.peaks) {
        // 从第一帧（最旧）和第三帧（最新）中选择目标峰值
        for (const auto& targetPeak1 : frame1.peaks) {
            for (const auto& targetPeak2 : frame3.peaks) {
                // 计算三帧组合哈希值，使用峰值的实际时间戳，而不是帧的时间戳
                uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);
                
                // 创建签名点
                SignaturePoint signaturePoint;
                signaturePoint.hash = hash;
                signaturePoint.timestamp = anchorPeak.timestamp; // 使用锚点峰值的精确时间戳
                signaturePoint.frequency = anchorPeak.frequency;
                signaturePoint.amplitude = static_cast<uint32_t>(anchorPeak.magnitude * 1000);
                
                if (signatures.size() == 147) {
                    int i = 0;
                }
                // 添加调试信息 - 确保std::hex只影响hash值的输出
                // if (signatures.size() < 5) {
                    // std::cout << "[Debug] 生成三帧指纹点: 锚点时间戳=" << anchorPeak.timestamp 
                    //           << "s, 目标1时间戳=" << targetPeak1.timestamp
                    //           << "s, 目标2时间戳=" << targetPeak2.timestamp
                    //           << "s, 哈希=0x" << std::hex << hash << std::dec 
                    //           << ", anchorPeak频率=" << anchorPeak.frequency << "Hz" 
                    //           << ", targetPeak1频率=" << targetPeak1.frequency << "Hz" 
                    //           << ", targetPeak2频率=" << targetPeak2.frequency << "Hz" 
                    //           << ", anchorPeak幅度=" << anchorPeak.magnitude
                    //           << ", targetPeak1幅度=" << targetPeak1.magnitude
                    //           << ", targetPeak2幅度=" << targetPeak2.magnitude
                    //           << ", anchorPeak时间戳=" << anchorPeak.timestamp << "s"
                    //           << ", targetPeak1时间戳=" << targetPeak1.timestamp << "s"
                    //           << ", targetPeak2时间戳=" << targetPeak2.timestamp << "s"
                    //           << std::endl;
                // }
                
                // Add to visualization data if enabled
                if (collectVisualizationData_) {
                    visualizationData_.fingerprintPoints.emplace_back(
                        signaturePoint.frequency, 
                        signaturePoint.timestamp, 
                        signaturePoint.hash
                    );
                }
                
                signatures.push_back(signaturePoint);
            }
        }
    }
    
    return signatures;
}

// 计算三帧组合哈希值
uint32_t SignatureGenerator::computeTripleFrameHash(
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
    int32_t quantizationStep = static_cast<int32_t>(frameDuration_ * 100);
    
    // 约束时间差范围，调整为适应当前帧时长的范围
    // 保持约束范围在[-32,31]，但量化步长随帧时长变化
    int32_t timeDelta1Constrained = std::max(-32, std::min(31, timeDelta1 / quantizationStep));
    int32_t timeDelta2Constrained = std::max(-32, std::min(31, timeDelta2 / quantizationStep));
    
    // 调整为无符号表示用于位操作
    uint32_t timeDelta1Unsigned = static_cast<uint32_t>(timeDelta1Constrained + 32) & 0x3F; // 6位
    uint32_t timeDelta2Unsigned = static_cast<uint32_t>(timeDelta2Constrained + 32) & 0x3F; // 6位
    
    // 处理频率差，限制在10位表示范围内
    uint32_t freqDelta1Mapped = static_cast<uint32_t>(std::abs(freqDelta1) & 0x1FF); // 9位幅度
    uint32_t freqDelta2Mapped = static_cast<uint32_t>(std::abs(freqDelta2) & 0x1FF); // 9位幅度
    
    // 保留符号位（第10位）
    if (freqDelta1 < 0) freqDelta1Mapped |= 0x200;
    if (freqDelta2 < 0) freqDelta2Mapped |= 0x200;
    
    // 创建第一组异或组合（频率差1和时间差1）
    // 使用异或运算结合频率和时间信息，增加区分度
    uint32_t combo1 = (freqDelta1Mapped & 0x3FF) ^ 
                     ((timeDelta1Unsigned & 0x03) << 8) ^ // 时间差低2位移到高位
                     ((timeDelta1Unsigned & 0x3C) << 2);  // 时间差高4位调整位置
    
    // 创建第二组异或组合（频率差2和时间差2）
    uint32_t combo2 = (freqDelta2Mapped & 0x3FF) ^ 
                     ((timeDelta2Unsigned & 0x03) << 8) ^ // 时间差低2位移到高位
                     ((timeDelta2Unsigned & 0x3C) << 2);  // 时间差高4位调整位置
    
    // 确保组合结果不超过10位
    combo1 &= 0x3FF;
    combo2 &= 0x3FF;
    
    // 最终的哈希组合
    uint32_t hash = ((anchorPeak.frequency & 0xFFF) << 20) | // 锚点频率 (12位) - 位置20-31
                   (combo1 << 10) |                         // 第一组组合 (10位) - 位置10-19
                   combo2;                                  // 第二组组合 (10位) - 位置0-9
    
    return hash;
}

std::vector<SignaturePoint> SignatureGenerator::signature() const {
    return signatures_;
}

void SignatureGenerator::resetSignatures() {
    signatures_.clear();
    frameHistoryMap_.clear();
    channelBuffers_.clear();
    fftResultsMap_.clear();
    
    // Reset visualization data
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
    }
    
    std::cout << "[Debug] 已重置所有签名、帧历史和通道缓冲区" << std::endl;
}

} // namespace afp 
