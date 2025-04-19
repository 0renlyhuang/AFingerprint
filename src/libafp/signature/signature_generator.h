#pragma once
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <deque>
#include "fft/fft_interface.h"
#include "debugger/audio_debugger.h"
#include "afp/iperformance_config.h"
#include "afp/pcm_format.h"
#include "audio/pcm_reader.h"
#include "afp/isignature_generator.h"

namespace afp {

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

// 帧结构，存储一个时间帧的峰值点
struct Frame {
    std::vector<Peak> peaks;
    double timestamp;     // 帧时间戳 (秒)
};

class SignatureGenerator : public ISignatureGenerator {
public:
    explicit SignatureGenerator(std::shared_ptr<IPerformanceConfig> config);
    ~SignatureGenerator() override;

    // 初始化生成器
    bool init(const PCMFormat& format) override;

    // 添加音频数据
    bool appendStreamBuffer(const void* buffer, 
                          size_t bufferSize,
                          double startTimestamp) override;

    // 获取生成的指纹
    std::vector<SignaturePoint> signature() const override;
    
    // 重置所有已生成的签名
    void resetSignatures() override;

private:
    // 从音频帧中提取峰值
    std::vector<Peak> extractPeaks(const float* buffer, double timestamp);
    
    // 从多帧峰值生成指纹 - 新方法（跨3帧）
    std::vector<SignaturePoint> generateTripleFrameSignatures(
        const std::deque<Frame>& frameHistory,
        double currentTimestamp);
    
    // 计算三帧组合哈希值
    uint32_t computeTripleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

private:
    static const size_t FRAME_BUFFER_SIZE = 3;  // 保存3帧用于生成指纹
    constexpr static const double FRAME_DURATION = 0.1;   // 每帧0.1秒

    size_t fftSize_;        // FFT窗口大小
    std::unique_ptr<FFTInterface> fft_;
    std::shared_ptr<IPerformanceConfig> config_;
    PCMFormat format_;
    size_t sampleRate_;
    std::unique_ptr<PCMReader> reader_;
    std::vector<SignaturePoint> signatures_;
    
    // 内部缓冲区
    std::vector<float> window_;
    std::vector<float> buffer_;
    std::vector<std::complex<float>> fftBuffer_;
    
    // 存储每个通道的历史帧的缓冲区，用于跨3帧生成指纹
    std::map<uint32_t, std::deque<Frame>> frameHistoryMap_;
    
    // 上一次处理的时间戳，用于确保按0.1秒分帧
    double lastProcessedTime_;
};

} // namespace afp 
