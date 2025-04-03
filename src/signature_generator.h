#pragma once
#include <vector>
#include <memory>
#include <string>
#include "fft/fft_interface.h"

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

private:
    // 计算单个时间点的指纹
    SignaturePoint computeSignaturePoint(const float* buffer, 
                                       size_t bufferSize,
                                       double timestamp);
                                     
    // 生成星座图哈希值
    std::vector<ConstellationPoint> generateConstellationHashes(
        const std::vector<float>& magnitudes,
        const std::vector<float>& frequencies,
        double timestamp);

    // 计算峰值对
    std::vector<std::pair<uint32_t, uint32_t>> findPeakPairs(
        const std::vector<float>& magnitudes,
        const std::vector<float>& frequencies);

    // 计算hash值
    uint32_t computeHash(uint32_t f1, uint32_t f2, uint32_t t);

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
};

} // namespace afp 