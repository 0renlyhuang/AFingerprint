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
                          
    // 从短帧FFT结果缓冲区中提取峰值 - 基于滑动窗口
    std::vector<Peak> extractPeaksFromFFTResults(
        const std::vector<FFTResult>& fftResults,
        double windowStartTime,
        double windowEndTime);

    // 从多帧峰值生成指纹 - 基于三帧组合的方法
    // 实现了基于频率差异、时间差异和幅度的过滤:
    // 1. 根据配置的minFreqDelta和maxFreqDelta过滤频率差异太小或太大的对
    // 2. 根据配置的maxTimeDelta过滤时间差异太大的对
    // 3. 根据幅度阈值过滤幅度太小的峰值点
    // 4. 确保不同帧之间的频率差异充分，避免生成冗余或相似的哈希值
    std::vector<SignaturePoint> generateTripleFrameSignatures(
        const std::deque<Frame>& frameHistory);
    
    // 计算三帧组合哈希值
    // 增强的哈希计算方法，结合了以下特征:
    // 1. 锚点峰值的频率作为基础特征
    // 2. 峰值之间的频率差异
    // 3. 峰值之间的时间差异
    // 4. 峰值之间的相对幅度差异
    // 通过结合这些特征，生成更具辨识度和稳定性的哈希值
    uint32_t computeTripleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

    // 处理长帧音频数据，基于滑动窗口
    void processLongFrame(uint32_t channel);
    
    // 基于滑动窗口检测峰值
    void detectPeaksInSlidingWindow(uint32_t channel);
    
    // 维护滑动窗口状态
    bool updateSlidingWindows(uint32_t channel, double timestamp);
    
    // 尝试从峰值缓存中生成长帧
    void tryGenerateLongFrames(uint32_t channel);
    
    // 清理过期的FFT和峰值数据
    void cleanupOldData(uint32_t channel, double oldestTimeToKeep);

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
    
    // 存储每个通道的历史帧的缓冲区，用于跨3帧生成指纹
    std::map<uint32_t, std::deque<Frame>> frameHistoryMap_;
    
    // 每个通道的固定大小缓冲区，用于存储正好一帧长度的数据
    std::map<uint32_t, ChannelBuffer> channelBuffers_;
    
    // 每个通道的短帧FFT结果缓冲区
    std::map<uint32_t, std::vector<FFTResult>> fftResultsMap_;
    
    // 每个通道的峰值缓存，存储通过峰值检测后的结果，用于积累长帧
    std::map<uint32_t, std::vector<Peak>> peakCache_;
    
    // 每个通道的已确认时间窗口信息
    std::map<uint32_t, SlidingWindowInfo> peakDetectionWindowMap_;
    
    // 每个通道的长帧滑动窗口信息
    std::map<uint32_t, SlidingWindowInfo> longFrameWindowMap_;
    
    // 记录每个通道最后处理的短帧时间戳
    std::map<uint32_t, double> lastProcessedShortFrameMap_;
    
    // 记录每个通道的已确认峰值窗口结束时间
    std::map<uint32_t, double> confirmedPeakWindowEndMap_;
    
    // Visualization data
    bool collectVisualizationData_ = false;
    VisualizationData visualizationData_;
};

} // namespace afp 
