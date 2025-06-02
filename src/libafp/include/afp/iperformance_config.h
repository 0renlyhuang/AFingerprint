#pragma once
#include <cstdint>
#include <string>
#include <memory>

namespace afp {

// 平台类型枚举
enum class PlatformType {
    Mobile,     // 移动端
    Desktop,    // PC端
    Server      // 服务器端
};

// FFT相关配置
struct FFTConfig {
    size_t fftSize;        // FFT窗口大小
    size_t hopSize;        // 帧移大小
};

// 峰值检测配置
struct PeakDetectionConfig {
    size_t localMaxRange;      // 检查频率维度本地最大值的范围
    size_t timeMaxRange;       // 检查时间维度本地最大值的范围
    float minPeakMagnitude;    // 最小峰值幅度阈值
    size_t minFreq;           // 最小频率 (Hz)
    size_t maxFreq;           // 最大频率 (Hz)
    double peakTimeDuration;  // 峰值检测时间窗口 (秒)
    float quantileThreshold;  // 分位数阈值 (0.0-1.0, 例如0.8表示80分位数)
    size_t numFrequencyBands; // 频段数量 (4-6个)
    
    // 动态峰值分配配置
    size_t minPeaksPerFrame;   // 每帧保留的最少峰值数
    size_t maxPeaksPerFrameLimit; // 每帧保留的最多峰值数上限
    double noiseEstimationWindow; // 噪声估计时间窗口 (秒)
    float snrThreshold;        // 信噪比阈值，低于此值的频段会被抑制
    float energyWeightFactor;  // 能量权重因子 (0.0-1.0)
    float snrWeightFactor;     // 信噪比权重因子 (0.0-1.0)
};

// 指纹生成配置
struct SignatureGenerationConfig {
    size_t minFreqDelta;      // 最小频率差 (Hz)
    size_t maxFreqDelta;      // 最大频率差 (Hz)
    double maxTimeDelta;      // 最大时间差 (秒)
    double frameDuration;     // 长帧持续时间 (秒)
    size_t maxDoubleFrameCombinations; // 每个双帧窗口保留的最大峰值组合数
    double minDoubleFrameScore; // 双帧峰值组合的最低评分阈值，低于此分数的组合将被丢弃
    size_t maxTripleFrameCombinations; // 每个三帧窗口保留的最大峰值组合数
    double minTripleFrameScore; // 三帧峰值组合的最低评分阈值，低于此分数的组合将被丢弃
    
    // 扩展三帧选取配置
    size_t symmetricFrameRange;   // 对称帧范围n，生成(x-n,x,x+n)到(x-1,x,x+1)的组合
};

// 匹配配置
struct MatchingConfig {
    size_t maxCandidates;         // 最大候选结果数
    size_t maxCandidatesPerSignature; // 每个signature的最大候选结果数
    double matchExpireTime;       // 匹配过期时间 (秒)
    float minConfidenceThreshold; // 最小置信度阈值
    size_t minMatchesRequired;    // 最小匹配点数要求
    size_t minMatchesUniqueTimestampRequired; // 最小unique时间戳数量要求
    double offsetTolerance;       // 时间偏移容忍度 (秒)
};

class IPerformanceConfig {
public:
    virtual ~IPerformanceConfig() = default;

    // 获取各个配置
    virtual const FFTConfig& getFFTConfig() const = 0;
    virtual const PeakDetectionConfig& getPeakDetectionConfig() const = 0;
    virtual const SignatureGenerationConfig& getSignatureGenerationConfig() const = 0;
    virtual const MatchingConfig& getMatchingConfig() const = 0;
};

} // namespace afp 