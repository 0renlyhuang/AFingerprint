#pragma once
#include <vector>
#include <memory>
#include <string>
#include "fft/fft_interface.h"
#include "debugger/audio_debugger.h"

namespace afp {

struct SignaturePoint {
    uint32_t hash;           // 音频指纹hash值
    double timestamp;        // 时间戳（秒）
    uint32_t frequency;      // 频率（Hz）
    uint32_t amplitude;      // 振幅
};

// 星座图中的锚点和目标点
struct ConstellationPoint {
    uint32_t hash;          // 哈希值
    uint32_t anchorFreq;    // 锚点频率
    uint32_t targetFreq;    // 目标点频率
    uint32_t deltaTime;     // 时间差 (毫秒)
    uint32_t amplitude;     // 振幅
};

// 定义峰值结构，用于跨帧存储
struct Peak {
    uint32_t frequency;   // 频率 (Hz)
    float magnitude;      // 幅度
    double timestamp;     // 时间戳 (秒)
};

class SignatureGenerator {
public:
    SignatureGenerator();
    ~SignatureGenerator();

    // 初始化生成器
    bool init(size_t fftSize = 2048, 
              size_t sampleRate = 44100,
              size_t hopSize = 512);

    // 添加音频数据
    bool appendStreamBuffer(const float* buffer, 
                          size_t bufferSize,
                          double startTimestamp);

    // 获取生成的指纹
    std::vector<SignaturePoint> signature() const;
    
    // 重置所有已生成的签名
    // 在开始新的音频流时调用
    void resetSignatures();

private:
    // 计算hash值
    uint32_t computeHash(uint32_t f1, uint32_t f2, uint32_t t);
    
    // 从音频帧中提取峰值
    std::vector<Peak> extractPeaks(const float* buffer, double timestamp);
    
    // 从峰值生成指纹
    std::vector<SignaturePoint> generateSignaturesFromPeaks(
        const std::vector<Peak>& currentPeaks, 
        const std::vector<Peak>& historyPeaks,
        double currentTimestamp);

private:
    std::unique_ptr<FFTInterface> fft_;
    size_t fftSize_;
    size_t sampleRate_;
    size_t hopSize_;
    std::vector<SignaturePoint> signatures_;
    
    // 内部缓冲区
    std::vector<float> window_;
    std::vector<float> buffer_;
    std::vector<std::complex<float>> fftBuffer_;
    
    // 存储历史峰值的缓冲区，用于跨帧生成指纹
    std::vector<Peak> peakHistory_;
    static const size_t MAX_PEAK_HISTORY = 20; // 保存最近20帧的峰值
};

} // namespace afp 