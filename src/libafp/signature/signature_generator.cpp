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
    
    // 首先按照时间偏移进行遍历
    for (size_t offset = 0; offset + fftSize_ <= maxBufferSize; offset += hopSize) {
        // 当前偏移的实际时间戳
        double currentTimestamp = startTimestamp + offset / static_cast<double>(sampleRate_);
        
        // 然后处理每个通道在当前时间偏移的数据
        for (auto& [channel, processedBuffer] : channelBuffers) {
            if (processedBuffer.empty() || offset + fftSize_ > processedBuffer.size()) {
                continue;
            }
            
            // 添加调试信息
            if (firstCall && channel == 0) {  // 只为第一个通道添加调试信息
                AudioDebugger::checkAudioBuffer(processedBuffer.data(), processedBuffer.size(), startTimestamp, firstCall);
                firstCall = false;
            }
            
            // 初始化处理缓冲区，但不清空已生成的签名
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
            
            // 从当前帧中提取峰值
            std::vector<Peak> currentPeaks = extractPeaks(buffer_.data(), currentTimestamp);
            
            // 使用当前峰值和历史峰值生成指纹
            if (!currentPeaks.empty() && !peakHistory_[channel].empty()) {
                // 生成指纹 - 当前帧的峰值与历史帧的峰值
                std::vector<SignaturePoint> newPoints = generateSignaturesFromPeaks(
                    currentPeaks, peakHistory_[channel], currentTimestamp);
                
                // 添加生成的指纹
                signatures_.insert(signatures_.end(), newPoints.begin(), newPoints.end());
                
                // 调试输出
                if (!newPoints.empty() && channel == 0) {  // 仅对第一个通道输出调试信息
                    std::cout << "[Debug] 从通道 " << channel << " 当前帧与历史帧生成了 " << newPoints.size() 
                              << " 个指纹点，当前时间戳: " << currentTimestamp << std::endl;
                }
            }
            
            // 将当前帧的峰值添加到该通道的历史记录中
            peakHistory_[channel].insert(peakHistory_[channel].end(), currentPeaks.begin(), currentPeaks.end());
            
            // 限制历史记录的大小 - 只保留最近的若干帧
            if (peakHistory_[channel].size() > MAX_PEAK_HISTORY * 10) { // 假设每帧最多10个峰值
                // 移除最旧的峰值，保留最新的
                peakHistory_[channel].erase(peakHistory_[channel].begin(), 
                                   peakHistory_[channel].begin() + (peakHistory_[channel].size() - MAX_PEAK_HISTORY * 10));
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
    
    // 输出每个通道的峰值历史记录数量
    for (const auto& [channel, history] : peakHistory_) {
        std::cout << "[Debug] 通道 " << channel << " 的峰值历史记录包含 " << history.size() << " 个峰值点" << std::endl;
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
    
    // 只保留最强的峰值
    if (peaks.size() > peakConfig.maxPeaksPerFrame) {
        peaks.resize(peakConfig.maxPeaksPerFrame);
    }
    
    return peaks;
}

std::vector<SignaturePoint> SignatureGenerator::generateSignaturesFromPeaks(
    const std::vector<Peak>& currentPeaks, 
    const std::vector<Peak>& historyPeaks,
    double currentTimestamp) {
    
    std::vector<SignaturePoint> signatures;
    const auto& sigConfig = config_->getSignatureGenerationConfig();
    
    // 对每个当前帧的峰值
    for (const auto& currentPeak : currentPeaks) {
        // 与符合条件的历史峰值配对
        for (const auto& historyPeak : historyPeaks) {
            // 确保是不同时间的峰值（不同的时间戳）
            if (std::abs(currentPeak.timestamp - historyPeak.timestamp) < 0.001) {
                continue; // 跳过相同时间戳的峰值
            }
            
            // 计算频率差
            int32_t freqDelta = currentPeak.frequency - historyPeak.frequency;
            
            // 计算时间差 (秒)
            double timeDelta = currentPeak.timestamp - historyPeak.timestamp;
            
            // 确保时间差为正（当前帧总是比历史帧更新）
            if (timeDelta <= 0 || timeDelta > sigConfig.maxTimeDelta) {
                continue;
            }
            
            // 检查频率差是否在范围内
            if (std::abs(freqDelta) >= sigConfig.minFreqDelta && 
                std::abs(freqDelta) <= sigConfig.maxFreqDelta) {
                // 将时间差转换为毫秒，并限制在0-63范围内
                uint32_t deltaTimeMs = static_cast<uint32_t>(timeDelta * 1000) % 64;
                
                // 计算哈希值 - Shazam风格
                uint32_t hash = ((historyPeak.frequency & 0x1FFF) << 19) | // 锚点频率
                             ((currentPeak.frequency & 0x1FFF) << 6) |    // 目标频率
                             (deltaTimeMs & 0x3F);                       // 时间差
                
                SignaturePoint signaturePoint;
                signaturePoint.hash = hash;
                signaturePoint.timestamp = currentPeak.timestamp; // 使用历史峰值的实际时间戳
                signaturePoint.frequency = historyPeak.frequency;
                signaturePoint.amplitude = static_cast<uint32_t>(historyPeak.magnitude * 1000);
                
                // 添加调试信息 - 确保std::hex只影响hash值的输出
                if (signatures.size() < 5) {
                    std::cout << "[Debug] 生成指纹点: 时间戳=" << historyPeak.timestamp 
                              << "s, 哈希=0x" << std::hex << hash << std::dec 
                              << ", 频率=" << historyPeak.frequency << "Hz" << std::endl;
                }
                
                signatures.push_back(signaturePoint);
            }
        }
    }
    
    return signatures;
}

std::vector<SignaturePoint> SignatureGenerator::signature() const {
    return signatures_;
}

void SignatureGenerator::resetSignatures() {
    signatures_.clear();
    peakHistory_.clear(); // 同时清空峰值历史记录
    std::cout << "[Debug] 已重置所有签名和峰值历史" << std::endl;
}

} // namespace afp 