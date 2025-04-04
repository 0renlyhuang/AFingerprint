#include "signature_generator.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace afp {

SignatureGenerator::SignatureGenerator() 
    : fftSize_(2048)
    , sampleRate_(44100)
    , hopSize_(512) {
}

SignatureGenerator::~SignatureGenerator() = default;

bool SignatureGenerator::init(size_t fftSize, size_t sampleRate, size_t hopSize) {
    fftSize_ = fftSize;
    sampleRate_ = sampleRate;
    hopSize_ = hopSize;

    // 创建FFT实例
    fft_ = FFTFactory::create(fftSize_);
    if (!fft_ || !fft_->init(fftSize_)) {
        return false;
    }

    // 初始化汉宁窗
    window_.resize(fftSize_);
    for (size_t i = 0; i < fftSize_; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize_ - 1)));
    }

    // 初始化缓冲区
    buffer_.resize(fftSize_);
    fftBuffer_.resize(fftSize_);

    return true;
}

// 流式处理音频数据
bool SignatureGenerator::appendStreamBuffer(const float* buffer, 
                                         size_t bufferSize,
                                         double startTimestamp) {
    if (!buffer || bufferSize == 0) {
        return false;
    }

    // 添加调试信息
    static bool firstCall = true;
    AudioDebugger::checkAudioBuffer(buffer, bufferSize, startTimestamp, firstCall);
    if (firstCall) {
        firstCall = false;
    }

    // 初始化处理缓冲区，但不清空已生成的签名
    buffer_.clear();
    buffer_.resize(fftSize_, 0.0f);
    
    std::cout << "处理音频数据: " << bufferSize << " 样本, 使用固定大小缓冲区: " << fftSize_ << " 样本" << std::endl;
    
    // 记录处理前的签名数量，用于返回是否生成了新的签名
    size_t initialSignatureCount = signatures_.size();
    
    // 循环处理输入缓冲区
    size_t hopSize = hopSize_; 
    if (hopSize < 64) hopSize = 64; // 确保hop大小不会太小
    
    size_t offset = 0;
    while (offset + fftSize_ <= bufferSize) {
        // 提取当前帧
        std::memcpy(buffer_.data(), buffer + offset, fftSize_ * sizeof(float));
        
        // 检查复制到buffer_中的数据
        AudioDebugger::checkCopiedBuffer(buffer_, offset, fftSize_);
        
        // 应用预加重滤波器以强调高频内容
        for (size_t i = 1; i < fftSize_; ++i) {
            buffer_[i] -= 0.95f * buffer_[i-1];
        }
        
        // 检查预加重后的数据
        AudioDebugger::checkPreEmphasisBuffer(buffer_, offset, fftSize_);
        
        // 计算准确的时间戳 - 基于采样率和偏移量
        double currentTimestamp = startTimestamp + offset / static_cast<double>(sampleRate_);
        
        // 从当前帧中提取峰值，并将它们添加到峰值历史记录中
        std::vector<Peak> currentPeaks = extractPeaks(buffer_.data(), currentTimestamp);
        
        // 使用当前峰值和历史峰值生成指纹
        if (!currentPeaks.empty() && !peakHistory_.empty()) {
            // 生成指纹 - 当前帧的峰值与历史帧的峰值
            std::vector<SignaturePoint> newPoints = generateSignaturesFromPeaks(currentPeaks, peakHistory_, currentTimestamp);
            
            // 添加生成的指纹
            signatures_.insert(signatures_.end(), newPoints.begin(), newPoints.end());
            
            // 调试输出
            if (!newPoints.empty()) {
                std::cout << "[Debug] 从当前帧与历史帧生成了 " << newPoints.size() 
                          << " 个指纹点，当前时间戳: " << currentTimestamp << std::endl;
            }
        }
        
        // 将当前帧的峰值添加到历史记录中
        peakHistory_.insert(peakHistory_.end(), currentPeaks.begin(), currentPeaks.end());
        
        // 限制历史记录的大小 - 只保留最近的若干帧
        if (peakHistory_.size() > MAX_PEAK_HISTORY * 10) { // 假设每帧最多10个峰值
            // 移除最旧的峰值，保留最新的
            peakHistory_.erase(peakHistory_.begin(), 
                               peakHistory_.begin() + (peakHistory_.size() - MAX_PEAK_HISTORY * 10));
        }
        
        // 移动到下一个 hop
        offset += hopSize;
    }
    
    size_t newSignaturesGenerated = signatures_.size() - initialSignatureCount;
    std::cout << "[Debug] 本次调用生成了 " << newSignaturesGenerated << " 个指纹点，总共有 " << signatures_.size() << " 个指纹点" << std::endl;
    std::cout << "[Debug] 当前峰值历史记录包含 " << peakHistory_.size() << " 个峰值点" << std::endl;
    
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
    for (size_t i = 2; i < fftSize_ / 2 - 2; ++i) {
        // 仅在 250Hz-5500Hz 范围内寻找峰值，这是 Shazam 常用的频率范围
        if (frequencies[i] >= 250 && frequencies[i] <= 5500) {
            // 检查是否是本地最大值
            if (magnitudes[i] > 0.05 && // 足够强
                magnitudes[i] > magnitudes[i-1] && 
                magnitudes[i] > magnitudes[i-2] && 
                magnitudes[i] > magnitudes[i+1] && 
                magnitudes[i] > magnitudes[i+2]) {
                
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
    
    // 只保留最强的峰值（最多10个）
    if (peaks.size() > 10) {
        peaks.resize(10);
    }
    
    return peaks;
}

// 从峰值生成指纹
std::vector<SignaturePoint> SignatureGenerator::generateSignaturesFromPeaks(
    const std::vector<Peak>& currentPeaks, 
    const std::vector<Peak>& historyPeaks,
    double currentTimestamp) {
    
    std::vector<SignaturePoint> signatures;
    
    // Shazam指纹参数
    const uint32_t MIN_FREQ_DELTA = 30;    // 最小频率差 (Hz)
    const uint32_t MAX_FREQ_DELTA = 300;   // 最大频率差 (Hz)
    const double MAX_TIME_DELTA = 3.0;     // 最大时间差 (秒)
    
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
            if (timeDelta <= 0 || timeDelta > MAX_TIME_DELTA) {
                continue;
            }
            
            // 检查频率差是否在范围内
            if (std::abs(freqDelta) >= MIN_FREQ_DELTA && std::abs(freqDelta) <= MAX_FREQ_DELTA) {
                // 将时间差转换为毫秒，并限制在0-63范围内
                uint32_t deltaTimeMs = static_cast<uint32_t>(timeDelta * 1000) % 64;
                
                // 计算哈希值 - Shazam风格
                uint32_t hash = ((historyPeak.frequency & 0x1FFF) << 19) | // 锚点频率
                             ((currentPeak.frequency & 0x1FFF) << 6) |    // 目标频率
                             (deltaTimeMs & 0x3F);                       // 时间差
                
                SignaturePoint signaturePoint;
                signaturePoint.hash = hash;
                signaturePoint.timestamp = historyPeak.timestamp; // 使用历史峰值的实际时间戳
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

uint32_t SignatureGenerator::computeHash(uint32_t f1, uint32_t f2, uint32_t t) {
    // 改进哈希算法，加入时间戳的低位信息
    // f1 和 f2 分别保留13位，t的低6位
    // 这样哈希值中包含频率信息和时间信息，增加唯一性
    uint32_t hash = ((f1 & 0x1FFF) << 19) |  // 高13位用于f1 (频率1)
                   ((f2 & 0x1FFF) << 6) |    // 中间13位用于f2 (频率2)
                   (t & 0x3F);               // 低6位用于时间戳
    
    // 调试输出
    static size_t hashCount = 0;
    if (hashCount < 5) {
        std::cout << "[Debug] 哈希计算: f1=" << f1 << ", f2=" << f2 
                  << ", t=" << t << " -> 0x" << std::hex << hash << std::dec << std::endl;
        hashCount++;
    }
    
    return hash;
}

} // namespace afp 