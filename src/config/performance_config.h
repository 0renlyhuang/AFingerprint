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

class PerformanceConfig {
public:
    // 获取指定平台的配置
    static std::shared_ptr<PerformanceConfig> getConfig(PlatformType platform);

    // FFT相关配置
    struct FFTConfig {
        size_t fftSize;        // FFT窗口大小
        size_t hopSize;        // 帧移大小
    };

    // 峰值检测配置
    struct PeakDetectionConfig {
        size_t localMaxRange;      // 检查本地最大值的范围
        size_t maxPeaksPerFrame;   // 每帧保留的最强峰值数
        float minPeakMagnitude;    // 最小峰值幅度阈值
        size_t minFreq;           // 最小频率 (Hz)
        size_t maxFreq;           // 最大频率 (Hz)
    };

    // 指纹生成配置
    struct SignatureGenerationConfig {
        size_t minFreqDelta;      // 最小频率差 (Hz)
        size_t maxFreqDelta;      // 最大频率差 (Hz)
        double maxTimeDelta;      // 最大时间差 (秒)
    };

    // 匹配配置
    struct MatchingConfig {
        size_t maxCandidates;         // 最大候选结果数
        double matchExpireTime;       // 匹配过期时间 (秒)
        float minConfidenceThreshold; // 最小置信度阈值
        size_t minMatchesRequired;    // 最小匹配点数要求
        double offsetTolerance;       // 时间偏移容忍度 (秒)
    };

    // 获取各个配置
    const FFTConfig& getFFTConfig() const { return fftConfig_; }
    const PeakDetectionConfig& getPeakDetectionConfig() const { return peakDetectionConfig_; }
    const SignatureGenerationConfig& getSignatureGenerationConfig() const { return signatureGenerationConfig_; }
    const MatchingConfig& getMatchingConfig() const { return matchingConfig_; }

private:
    // 默认构造函数
    PerformanceConfig() = default;

    // 创建不同平台的配置
    static std::shared_ptr<PerformanceConfig> createMobileConfig();
    static std::shared_ptr<PerformanceConfig> createDesktopConfig();
    static std::shared_ptr<PerformanceConfig> createServerConfig();

    // 配置参数
    FFTConfig fftConfig_;
    PeakDetectionConfig peakDetectionConfig_;
    SignatureGenerationConfig signatureGenerationConfig_;
    MatchingConfig matchingConfig_;
};

} // namespace afp 