#pragma once
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <deque>
#include "fft/fft_interface.h"
#include "debugger/audio_debugger.h"
#include "debugger/visualization.h"
#include "afp/iperformance_config.h"
#include "afp/pcm_format.h"
#include "audio/pcm_reader.h"
#include "afp/isignature_generator.h"

// 前向声明
namespace afp {
class PeekDetector;
class LongFrameBuilder;
class HashComputer;
}

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

// FFT结果结构，存储短帧的FFT结果
struct FFTResult {
    std::vector<float> magnitudes;
    std::vector<float> frequencies;
    double timestamp;     // 短帧时间戳 (秒)
};

// 通道缓冲区结构，存储一个通道的数据并跟踪已消费样本数
struct ChannelBuffer {
    std::vector<float> samples;  // 固定大小为一帧所需样本数
    size_t consumed;             // 已经消费的样本数
    double timestamp;            // 第一个样本的时间戳
    
    ChannelBuffer() : consumed(0), timestamp(0.0) {}
};

// 滑动窗口管理信息
struct SlidingWindowInfo {
    double currentWindowStart;      // 当前滑动窗口的起始时间戳
    double currentWindowEnd;        // 当前滑动窗口的结束时间戳
    double lastProcessedTime;       // 最后处理的时间戳
    double nextWindowStartTime;     // 下一个窗口的起始时间戳
    bool windowReady;               // 窗口是否准备好处理
    
    SlidingWindowInfo() : 
        currentWindowStart(0.0),
        currentWindowEnd(0.0),
        lastProcessedTime(0.0),
        nextWindowStartTime(0.0),
        windowReady(false) {}
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
    
    // Visualization methods
    // Enable/disable visualization data collection
    void enableVisualization(bool enable) {
        collectVisualizationData_ = enable;
    }
    
    // Get visualization data
    VisualizationData getVisualizationData() const {
        return visualizationData_;
    }
    
    // Set title for visualization
    void setVisualizationTitle(const std::string& title) {
        visualizationData_.title = title;
    }
    
    // Set audio file path for visualization
    void setAudioFilePath(const std::string& path) {
        visualizationData_.audioFilePath = path;
    }
    
    // Generate visualization and save to file
    bool saveVisualization(const std::string& filename) const;

private:
    // 处理短帧FFT分析
    void processShortFrame(const float* frameBuffer, 
                          uint32_t channel,
                          double frameTimestamp);

    // 清理过期的FFT数据
    void cleanupOldFFTData(uint32_t channel, int fftLastConsumedCount);

private:
    static const size_t FRAME_BUFFER_SIZE = 3;  // 保存3帧用于生成指纹
    double frameDuration_;   // 长帧持续时间，由配置决定
    double peakTimeDuration_; // 峰值检测时间窗口，可以与长帧时长不同

    size_t fftSize_;        // FFT窗口大小
    size_t hopSize_;        // 帧移大小（为实现重叠帧）
    size_t samplesPerFrame_; // 每帧所需的样本数量
    size_t samplesPerShortFrame_; // 每个短帧所需的样本数量
    size_t shortFramesPerLongFrame_; // 每个长帧包含的短帧数量
    
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
    
    // 每个通道的固定大小缓冲区，用于存储正好一帧长度的数据
    std::map<uint32_t, ChannelBuffer> channelBuffers_;
    
    // 每个通道的短帧FFT结果缓冲区
    std::map<uint32_t, std::vector<FFTResult>> fftResultsMap_;
    
    // 记录每个通道最后处理的短帧时间戳
    std::map<uint32_t, double> lastProcessedShortFrameMap_;
    
    // 逻辑处理类
    std::unique_ptr<PeekDetector> peekDetector_;
    std::unique_ptr<LongFrameBuilder> longFrameBuilder_;
    std::unique_ptr<HashComputer> hashComputer_;
    
    // Visualization data
    bool collectVisualizationData_ = false;
    VisualizationData visualizationData_;
};

} // namespace afp 
