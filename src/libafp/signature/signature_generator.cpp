#include "signature/signature_generator.h"
#include "fft/fft_interface.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <unordered_set>

// Include new header files for the logical classes
#include "peek_detector.h"
#include "long_frame_builder.h"
#include "hash_computer.h"

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
    , peakTimeDuration_(config->getPeakDetectionConfig().peakTimeDuration)  // 从配置中获取峰值检测窗口
    , samplesPerFrame_(0)  // 将在init中设置
    , samplesPerShortFrame_(0)  // 将在init中设置
    , shortFramesPerLongFrame_(0)  // 将在init中设置
    , fft_(FFTFactory::create(fftSize_))
    , peekDetector_(std::make_unique<PeekDetector>(config, &collectVisualizationData_, &visualizationData_))
    , longFrameBuilder_(std::make_unique<LongFrameBuilder>(config))
    , hashComputer_(std::make_unique<HashComputer>(config, &collectVisualizationData_, &visualizationData_)) {
    
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
    
    // 如果未设置峰值检测时间窗口，使用默认值
    if (peakTimeDuration_ <= 0) {
        peakTimeDuration_ = frameDuration_ * 0.7; // 默认使用长帧时长的70%
        std::cout << "[Debug] 峰值检测时间窗口未设置，使用默认值: " << peakTimeDuration_ << "s" << std::endl;
    }
    
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
    
    // 计算每个长帧需要的短帧数量 (理论上)
    // 这个值现在只用于初始化信息输出，不再作为判断长帧累积的标准
    // 实际处理中会根据时间戳来确定长帧边界
    shortFramesPerLongFrame_ = static_cast<size_t>(std::ceil(frameDuration_ / shortFrameDuration));
    
    // 清空所有缓冲区和历史记录
    channelBuffers_.clear();
    fftResultsMap_.clear();
    lastProcessedShortFrameMap_.clear();
    
    // Reset visualization data
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
        visualizationData_.duration = 0.0;  // Will be updated during processing
    }
    
    std::cout << "[Debug] 初始化完成: 采样率=" << sampleRate_ 
              << "Hz, 长帧时长=" << frameDuration_
              << "s, 峰值检测时间窗口=" << peakTimeDuration_
              << "s, 每长帧样本数=" << samplesPerFrame_
              << ", 每短帧样本数=" << samplesPerShortFrame_
              << ", 帧移大小=" << hopSize_
              << ", 帧重叠率=" << (1.0 - static_cast<double>(hopSize_) / fftSize_) * 100.0 << "%"
              << ", 每短帧持续时间=" << shortFrameDuration
              << "s, 理论上每长帧包含短帧数=" << shortFramesPerLongFrame_
              << " (实际根据时间戳确定), FFT大小=" << fftSize_ 
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

// 清理过期的FFT数据
void SignatureGenerator::cleanupOldFFTData(uint32_t channel, int fftLastConsumedCount) {
        // 根据fftLastConsumedCount和peakConfig.timeMaxRange计算出最多可以移除多少个峰值
    const auto& peakConfig = config_->getPeakDetectionConfig();
    const int maxCountToRemoveToSatisfyTimeMaxRange = fftResultsMap_[channel].size() - peakConfig.timeMaxRange;

    const auto firstNToRemove = std::min(maxCountToRemoveToSatisfyTimeMaxRange, fftLastConsumedCount);

    // 移除最早的firstNToRemove个峰值
    fftResultsMap_[channel].erase(fftResultsMap_[channel].begin(), fftResultsMap_[channel].begin() + firstNToRemove);

    std::cout << "[DEBUG-清理] SignatureGenerator: 通道" << channel 
            << "移除了" << firstNToRemove << "个短帧FFT结果" << std::endl;
}

// 流式处理音频数据 - 使用三个逻辑类
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
            lastProcessedShortFrameMap_[channel] = startTimestamp;
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
                
                // 步骤1: 使用PeekDetector进行峰值检测
                auto peakDetectionResult = peekDetector_->recvFFTResult(
                    channel, 
                    fftResultsMap_[channel], 
                    lastProcessedShortFrameMap_[channel]
                );

                // 如果峰值检测满足条件，尝试生成长帧
                if (peakDetectionResult.isPeekDetectionSatisfied) {
                    cleanupOldFFTData(channel, peakDetectionResult.fftConsumedCount);

                    // 获取当前通道的峰值缓存
                    const auto& peakCache = peekDetector_->getPeakCache(channel);
                    
                    // 步骤2: 使用LongFrameBuilder构建长帧
                    auto longFrameResult = longFrameBuilder_->buildLongFrame(
                        channel, 
                        peakCache, 
                        peakDetectionResult.lastConfirmTime
                    );
                    
                    // 如果长帧构建成功，尝试生成哈希值
                    if (longFrameResult.isLongFrameBuilt) {
                        peekDetector_->erasePeakCache(channel, longFrameResult.longFrameTimestamp);

                        auto hashResult = hashComputer_->computeHash(longFrameBuilder_->getLongFrames(channel), signatures_);
                        if (hashResult.isHashComputed) {
                            std::cout << "[DEBUG-指纹] SignatureGenerator: 通道" << channel 
                                        << "生成了新的指纹点，当前总数: " << signatures_.size() << std::endl;
                        }

                        longFrameBuilder_->removeConsumedLongFrame(channel);
                    }
                }
            }
        }
    }
    
    // 对signatures_根据hash+timestamp去重，保持顺序不变
    {
        if (signatures_.size() > initialSignatureCount) {
            // 使用unordered_set作为查询结构以获得O(1)的查找性能
            struct PairHash {
                size_t operator()(const std::pair<uint32_t, double>& p) const {
                    // 组合两个哈希值
                    return std::hash<uint32_t>{}(p.first) ^ 
                           (std::hash<double>{}(p.second) << 1);
                }
            };
            
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
    std::cout << "[DEBUG-统计] 本次调用生成了 " << newSignaturesGenerated << " 个指纹点，总共有 " 
              << signatures_.size() << " 个指纹点" << std::endl;
    
    // 输出每个通道的缓冲区状态
    for (const auto& [channel, buffer] : channelBuffers_) {
        std::cout << "[Debug] 通道 " << channel << " 的缓冲区已消费 " 
                  << buffer.consumed << "/" << samplesPerShortFrame_ 
                  << " 样本，缓冲区时间戳: " << buffer.timestamp << std::endl;
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
    
    // 更新最后处理的短帧时间戳
    lastProcessedShortFrameMap_[channel] = frameTimestamp;
}




std::vector<SignaturePoint> SignatureGenerator::signature() const {
    return signatures_;
}

void SignatureGenerator::resetSignatures() {
    signatures_.clear();
    channelBuffers_.clear();
    fftResultsMap_.clear();
    lastProcessedShortFrameMap_.clear();
    
    // 重置三个逻辑类
    if (peekDetector_) peekDetector_->reset();
    if (longFrameBuilder_) longFrameBuilder_->reset();
    
    // Reset visualization data
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
    }
    
    std::cout << "[Debug] 已重置所有签名、帧历史和通道缓冲区" << std::endl;
}


} // namespace afp 


