#include "signature/signature_generator.h"
#include "fft/fft_interface.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <unordered_set>

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
    
    // 计算每个长帧需要的样本数（0.1秒/帧）
    samplesPerFrame_ = static_cast<size_t>(FRAME_DURATION * sampleRate_);
    
    // 计算每个短帧需要的样本数（0.02秒/短帧）
    samplesPerShortFrame_ = static_cast<size_t>(SHORT_FRAME_DURATION * sampleRate_);
    
    // 计算每个长帧包含的短帧数
    shortFramesPerLongFrame_ = static_cast<size_t>(FRAME_DURATION / SHORT_FRAME_DURATION);
    
    // 确保samplesPerShortFrame_至少等于fftSize_
    if (samplesPerShortFrame_ < fftSize_) {
        samplesPerShortFrame_ = fftSize_;
    }
    
    // 清空所有缓冲区和历史记录
    frameHistoryMap_.clear();
    channelBuffers_.clear();
    fftResultsMap_.clear();
    
    std::cout << "[Debug] 初始化完成: 采样率=" << sampleRate_ 
              << "Hz, 每长帧样本数=" << samplesPerFrame_
              << ", 每短帧样本数=" << samplesPerShortFrame_
              << ", 每长帧包含短帧数=" << shortFramesPerLongFrame_
              << ", FFT大小=" << fftSize_ << std::endl;
              
    return true;
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
                processShortFrame(channelBuffer.samples.data(), channel, channelBuffer.timestamp);
                
                // 重置缓冲区
                channelBuffer.consumed = 0;
                
                // 更新时间戳为下一个短帧的开始时间
                channelBuffer.timestamp += SHORT_FRAME_DURATION;
                
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
    
    if (fftResults.empty()) {
        return peaks;
    }
    
    const auto& peakConfig = config_->getPeakDetectionConfig();
    
    // 使用峰值保留法而非平均法 - 保留每个频率bin的最大值，增强瞬态信号特征
    std::vector<float> maxMagnitudes(fftSize_ / 2, 0.0f);
    std::vector<float> frequencies = fftResults[0].frequencies; // 频率对于所有FFT是一致的
    
    // 对每个频率bin，保留所有短帧中的最大值
    for (const auto& fftResult : fftResults) {
        for (size_t i = 0; i < fftSize_ / 2; ++i) {
            maxMagnitudes[i] = std::max(maxMagnitudes[i], fftResult.magnitudes[i]);
        }
    }
    
    // 查找峰值 - 只选取强度足够的本地最大值
    for (size_t i = peakConfig.localMaxRange; i < fftSize_ / 2 - peakConfig.localMaxRange; ++i) {
        // 检查频率范围
        if (frequencies[i] >= peakConfig.minFreq && frequencies[i] <= peakConfig.maxFreq) {
            // 检查是否是本地最大值
            bool isPeak = true;
            for (size_t j = 1; j <= peakConfig.localMaxRange; ++j) {
                if (maxMagnitudes[i] <= maxMagnitudes[i-j] || maxMagnitudes[i] <= maxMagnitudes[i+j]) {
                    isPeak = false;
                    break;
                }
            }
            
            // 检查是否超过最小幅度阈值
            if (isPeak && maxMagnitudes[i] >= peakConfig.minPeakMagnitude) {
                Peak peak;
                peak.frequency = static_cast<uint32_t>(frequencies[i]);
                peak.magnitude = maxMagnitudes[i];
                peak.timestamp = longFrameTimestamp;
                
                peaks.push_back(peak);
            }
        }
    }
    
    // 按振幅从大到小排序
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
        return a.magnitude > b.magnitude;
    });
    
    // 确保至少有minPeaks个峰值，但不超过maxPeaks个
    if (peaks.size() > peakConfig.maxPeaksPerFrame) {
        peaks.resize(peakConfig.maxPeaksPerFrame);
    }
    
    return peaks;
}

// 处理长帧音频数据，从短帧FFT结果中提取峰值
void SignatureGenerator::processLongFrame(
                               uint32_t channel,
                               double frameTimestamp) {
    
    // 确保有足够的短帧FFT结果用于提取峰值
    if (fftResultsMap_[channel].size() < shortFramesPerLongFrame_) {
        return;
    }
    
    // 获取该通道的所有短帧FFT结果
    std::vector<FFTResult> fftResults(fftResultsMap_[channel].begin(), 
                                      fftResultsMap_[channel].begin() + shortFramesPerLongFrame_);
    
    // 从短帧FFT结果中提取峰值
    std::vector<Peak> currentPeaks = extractPeaksFromFFTResults(fftResults, frameTimestamp);
    
    if (!currentPeaks.empty()) {
        // 创建新帧并存储其峰值
        Frame newFrame;
        newFrame.peaks = currentPeaks;
        newFrame.timestamp = frameTimestamp;
        
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
            if (!newPoints.empty()) {
                std::cout << "[Debug] 从通道 " << channel << " 的三帧生成了 " << newPoints.size() 
                          << " 个指纹点，当前时间戳: " << frameTimestamp << std::endl;
            }
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
                // 计算三帧组合哈希值
                uint32_t hash = computeTripleFrameHash(anchorPeak, targetPeak1, targetPeak2);
                
                // 创建签名点
                SignaturePoint signaturePoint;
                signaturePoint.hash = hash;
                signaturePoint.timestamp = anchorPeak.timestamp; // 使用锚点帧的时间戳
                signaturePoint.frequency = anchorPeak.frequency;
                signaturePoint.amplitude = static_cast<uint32_t>(anchorPeak.magnitude * 1000);
                
                // 添加调试信息 - 确保std::hex只影响hash值的输出
                if (signatures.size() < 5) {
                    std::cout << "[Debug] 生成三帧指纹点: 时间戳=" << anchorPeak.timestamp 
                              << "s, 哈希=0x" << std::hex << hash << std::dec 
                              << ", 频率=" << anchorPeak.frequency << "Hz" << std::endl;
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
    uint32_t timeDelta1 = static_cast<uint32_t>((anchorPeak.timestamp - targetPeak1.timestamp) * 1000) % 64;
    uint32_t timeDelta2 = static_cast<uint32_t>((targetPeak2.timestamp - anchorPeak.timestamp) * 1000) % 64;
    
    // 组合哈希值 - 使用锚点频率、两个频率差和两个时间差
    uint32_t hash = ((anchorPeak.frequency & 0xFFF) << 20) |   // 锚点频率 (12位)
                   ((freqDelta1 & 0x3FF) << 10) |             // 第一个频率差 (10位)
                   ((freqDelta2 & 0x3FF) << 0) |              // 第二个频率差 (10位)
                   ((timeDelta1 & 0x3F) << 26) |              // 第一个时间差 (6位)
                   ((timeDelta2 & 0x3F) << 32);               // 第二个时间差 (6位)
    
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
    std::cout << "[Debug] 已重置所有签名、帧历史和通道缓冲区" << std::endl;
}

} // namespace afp 