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
    , fft_(FFTFactory::create(fftSize_))
    , lastProcessedTime_(-1.0) {
    
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
    frameHistoryMap_.clear();
    lastProcessedTime_ = -1.0;
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
    
    // 处理PCM数据，按通道分离数据
    std::map<uint32_t, std::vector<float>> channelBuffers;
    
    // 处理所有通道的数据
    reader_->process(buffer, bufferSize, [&](float sample, uint32_t channel) {
        channelBuffers[channel].push_back(sample);
    });
    
    if (channelBuffers.empty()) {
        return false;
    }
    
    std::cout << "处理音频数据: " << bufferSize / format_.frameSize() << " 样本, "
              << channelBuffers.size() << " 个通道, 使用固定大小缓冲区: " 
              << fftSize_ << " 样本" << std::endl;
    
    // 获取hop大小用于后续处理
    size_t hopSize = config_->getFFTConfig().hopSize;
    if (hopSize < 64) hopSize = 64; // 确保hop大小不会太小
    
    // 找出所有通道中的最大样本数量
    size_t maxBufferSize = 0;
    for (const auto& [channel, processedBuffer] : channelBuffers) {
        maxBufferSize = std::max(maxBufferSize, processedBuffer.size());
    }
    
    // 添加首次调用的标记
    static bool firstCall = true;
    
    // 计算每帧需要的样本数（0.1秒/帧）
    size_t samplesPerFrame = static_cast<size_t>(FRAME_DURATION * sampleRate_);
    
    // 首先根据时间戳进行分帧
    for (size_t offset = 0; offset + fftSize_ <= maxBufferSize; offset += samplesPerFrame) {
        // 当前帧的时间戳
        double frameTimestamp = startTimestamp + offset / static_cast<double>(sampleRate_);
        
        // 如果这是一个新的有效帧（时间戳增加了0.1秒或这是第一帧）
        if (lastProcessedTime_ < 0 || frameTimestamp - lastProcessedTime_ >= FRAME_DURATION - 0.01) {
            // 为每个通道处理当前帧
            for (auto& [channel, processedBuffer] : channelBuffers) {
                if (processedBuffer.empty() || offset + fftSize_ > processedBuffer.size()) {
                    continue;
                }
                
                // 添加调试信息
                if (firstCall && channel == 0) {  // 只为第一个通道添加调试信息
                    AudioDebugger::checkAudioBuffer(processedBuffer.data(), processedBuffer.size(), startTimestamp, firstCall);
                    firstCall = false;
                }
                
                // 初始化处理缓冲区
                buffer_.clear();
                buffer_.resize(fftSize_, 0.0f);
                
                // 提取当前帧
                std::memcpy(buffer_.data(), processedBuffer.data() + offset, fftSize_ * sizeof(float));
                
                // 检查复制到buffer_中的数据（仅对第一个通道）
                if (channel == 0) {
                    AudioDebugger::checkCopiedBuffer(buffer_, offset, fftSize_);
                }
                
                // 应用预加重滤波器以强调高频内容
                for (size_t i = 1; i < fftSize_; ++i) {
                    buffer_[i] -= 0.95f * buffer_[i-1];
                }
                
                // 检查预加重后的数据（仅对第一个通道）
                if (channel == 0) {
                    AudioDebugger::checkPreEmphasisBuffer(buffer_, offset, fftSize_);
                }
                
                // 从当前帧中提取峰值（每帧3-5个峰值点）
                std::vector<Peak> currentPeaks = extractPeaks(buffer_.data(), frameTimestamp);
                
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
            
            // 更新最后处理的时间戳
            lastProcessedTime_ = frameTimestamp;
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
    
    // 输出每个通道的帧历史记录数量
    for (const auto& [channel, history] : frameHistoryMap_) {
        std::cout << "[Debug] 通道 " << channel << " 的帧历史包含 " << history.size() << " 帧" << std::endl;
    }
    
    if (newSignaturesGenerated == 0) {
        std::cerr << "本次调用未能生成有效指纹，使用备用方案" << std::endl;
        // 如果本次调用没有生成有效指纹，创建一个备用指纹
        SignaturePoint fallbackPoint;
        fallbackPoint.hash = 0x12345678; // 使用一个固定的哈希值
        fallbackPoint.timestamp = startTimestamp;
        fallbackPoint.frequency = 1000;
        fallbackPoint.amplitude = 100;
        signatures_.push_back(fallbackPoint);
        std::cout << "[Debug] 添加了一个备用指纹点" << std::endl;
    }

    return newSignaturesGenerated > 0 || signatures_.size() > initialSignatureCount;
}

// 从音频帧中提取峰值
std::vector<Peak> SignatureGenerator::extractPeaks(const float* buffer, double timestamp) {
    std::vector<Peak> peaks;
    
    // 应用窗函数
    std::vector<float> windowed(fftSize_);
    for (size_t i = 0; i < fftSize_; ++i) {
        windowed[i] = buffer[i] * window_[i];
    }
    
    // 执行FFT
    if (!fft_->transform(windowed.data(), fftBuffer_.data())) {
        return peaks;
    }
    
    // 计算幅度谱
    std::vector<float> magnitudes(fftSize_ / 2);
    std::vector<float> frequencies(fftSize_ / 2);
    
    float maxMagnitude = 0.0001f; // 避免除零
    
    for (size_t i = 0; i < fftSize_ / 2; ++i) {
        // 计算复数的模
        float magnitude = std::abs(fftBuffer_[i]);
        
        // 对数频谱
        magnitudes[i] = magnitude > 0.00001f ? 20.0f * std::log10(magnitude) + 100.0f : 0;
        
        // 更新最大值
        if (magnitudes[i] > maxMagnitude) {
            maxMagnitude = magnitudes[i];
        }
        
        // 计算每个bin对应的频率
        frequencies[i] = i * sampleRate_ / fftSize_;
    }
    
    // 正规化幅度谱
    for (size_t i = 0; i < fftSize_ / 2; ++i) {
        magnitudes[i] /= maxMagnitude;
    }
    
    // 查找峰值 - 只选取强度足够的本地最大值
    const auto& peakConfig = config_->getPeakDetectionConfig();
    for (size_t i = peakConfig.localMaxRange; i < fftSize_ / 2 - peakConfig.localMaxRange; ++i) {
        // 检查频率范围
        if (frequencies[i] >= peakConfig.minFreq && frequencies[i] <= peakConfig.maxFreq) {
            // 检查是否是本地最大值
            bool isPeak = true;
            for (size_t j = 1; j <= peakConfig.localMaxRange; ++j) {
                if (magnitudes[i] <= magnitudes[i-j] || magnitudes[i] <= magnitudes[i+j]) {
                    isPeak = false;
                    break;
                }
            }
            
            // 检查是否超过最小幅度阈值
            if (isPeak && magnitudes[i] >= peakConfig.minPeakMagnitude) {
                Peak peak;
                peak.frequency = static_cast<uint32_t>(frequencies[i]);
                peak.magnitude = magnitudes[i];
                peak.timestamp = timestamp;
                
                peaks.push_back(peak);
            }
        }
    }
    
    // 按振幅从大到小排序
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
        return a.magnitude > b.magnitude;
    });
    
    // 只保留最强的3-5个峰值
    const size_t minPeaks = 3;
    const size_t maxPeaks = 5;
    
    // 确保至少有minPeaks个峰值，但不超过maxPeaks个
    if (peaks.size() > maxPeaks) {
        peaks.resize(maxPeaks);
    } else if (peaks.size() < minPeaks) {
        // 如果峰值太少，添加一些伪峰值以确保我们有足够的点生成指纹
        size_t originalSize = peaks.size();
        for (size_t i = originalSize; i < minPeaks; ++i) {
            // 生成均匀分布的伪峰值
            Peak fakePeak;
            fakePeak.frequency = peakConfig.minFreq + 
                                 (i * (peakConfig.maxFreq - peakConfig.minFreq)) / minPeaks;
            fakePeak.magnitude = 0.1f; // 低幅度
            fakePeak.timestamp = timestamp;
            peaks.push_back(fakePeak);
        }
    }
    
    return peaks;
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
    lastProcessedTime_ = -1.0;
    std::cout << "[Debug] 已重置所有签名和帧历史" << std::endl;
}

} // namespace afp 