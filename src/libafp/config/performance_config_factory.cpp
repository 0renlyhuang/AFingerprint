#include "afp/performance_config_factory.h"
#include "performance_config.h"

namespace afp {

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::getConfig(PlatformType platform) {
    switch (platform) {
        case PlatformType::Mobile:
            return createMobileConfig();
        case PlatformType::Desktop:
            return createDesktopConfig();
        case PlatformType::Server:
            return createServerConfig();
        default:
            return createDesktopConfig();
    }
}

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createMobileConfig() {
     auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 移动端使用较小的窗口以节省内存和计算资源
    config->fftConfig_.fftSize = 1024;    // 较小的FFT窗口
    config->fftConfig_.hopSize = 441;     // 0.1秒/帧 (44.1kHz采样率下约为441样本)
    
    // 峰值检测配置 - 针对每帧3-5个峰值的要求优化
    config->peakDetectionConfig_.localMaxRange = 5;        // 较小的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 4;         // 默认为1，只与相邻帧比较
    config->peakDetectionConfig_.maxPeaksPerFrame = 10;     // 每帧最多7个峰值
    config->peakDetectionConfig_.minPeakMagnitude = 25.0f;  // 提高阈值到45dB，有效过滤噪声
    config->peakDetectionConfig_.minFreq = 40;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5000;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.1;   // 移动端使用较小的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.75f; // 移动端使用75分位数，平衡性能和准确性
    config->peakDetectionConfig_.numFrequencyBands = 6;    // 移动端使用4个频段，平衡性能和准确性
    
    // 指纹生成配置 - 针对三帧组合哈希优化
    config->signatureGenerationConfig_.minFreqDelta = 60;   // 最小频率差，增加区分度
    config->signatureGenerationConfig_.maxFreqDelta = 3000;  // 最大频率差，避免跨度太大
    config->signatureGenerationConfig_.maxTimeDelta = 0.3;  // 最大时间差限制为0.2秒，增强时间相关性
    config->signatureGenerationConfig_.frameDuration = 0.08; // 移动端使用较短的长帧时长，优化性能
    
    // 匹配配置 - 移动端使用较严格的参数以减少内存使用
    config->matchingConfig_.maxCandidates = 20;            // 较少的候选结果
    config->matchingConfig_.matchExpireTime = 60.0;         // 较短的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.5;  // 较高的置信度阈值
    config->matchingConfig_.minMatchesRequired = 5;       // 减少最小匹配点数要求
    config->matchingConfig_.offsetTolerance = 1.5;        // 较大的时间偏移容忍度
    
    return config;
}

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createDesktopConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - PC端使用中等大小的窗口
    config->fftConfig_.fftSize = 2048;    // 中等FFT窗口
    config->fftConfig_.hopSize = 512;     // 中等帧移
    
    // 峰值检测配置 - PC端使用中等参数
    config->peakDetectionConfig_.localMaxRange = 3;        // 中等本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 2;         // 中等时间维度最大值范围，前后2帧
    config->peakDetectionConfig_.maxPeaksPerFrame = 10;    // 每帧保留中等数量的峰值
    config->peakDetectionConfig_.minPeakMagnitude = 40.0f; // 提高阈值到40dB，平衡敏感度和抗噪性
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5500;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.25;  // PC端使用中等峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.8f;  // 桌面端使用80分位数，提高峰值质量
    config->peakDetectionConfig_.numFrequencyBands = 5;    // 桌面端使用5个频段，提高精度
    
    // 指纹生成配置 - PC端使用中等参数
    config->signatureGenerationConfig_.minFreqDelta = 70;   // 中等最小频率差，增强区分度
    config->signatureGenerationConfig_.maxFreqDelta = 600;  // 中等最大频率差，允许更广范围的匹配
    config->signatureGenerationConfig_.maxTimeDelta = 0.3;  // 中等最大时间差，增强时序严谨性
    config->signatureGenerationConfig_.frameDuration = 0.18; // PC端使用中等长帧时长，平衡准确率和性能
    
    // 匹配配置 - PC端使用中等参数
    config->matchingConfig_.maxCandidates = 50;            // 中等候选结果数
    config->matchingConfig_.matchExpireTime = 5.0;         // 中等过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.4;  // 中等置信度阈值
    config->matchingConfig_.minMatchesRequired = 15;       // 中等最小匹配点数
    config->matchingConfig_.offsetTolerance = 0.1;         // 中等时间偏移容忍度
    
    return config;
}

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createServerConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 服务器端使用较大的窗口以获得更好的频率分辨率
    config->fftConfig_.fftSize = 4096;    // 较大的FFT窗口
    config->fftConfig_.hopSize = 1024;    // 较大的帧移
    
    // 峰值检测配置 - 服务器端使用较严格的参数
    config->peakDetectionConfig_.localMaxRange = 4;        // 较大的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 3;         // 较大的时间维度最大值范围，前后3帧
    config->peakDetectionConfig_.maxPeaksPerFrame = 15;    // 每帧保留较多的峰值
    config->peakDetectionConfig_.minPeakMagnitude = 35.0f; // 设置为35dB，服务器端更敏感以捕获更多细节
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 6000;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.3;   // 服务器端使用较大的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.85f; // 服务器端使用85分位数，最高峰值质量
    config->peakDetectionConfig_.numFrequencyBands = 6;    // 服务器端使用6个频段，最高精度
    
    // 指纹生成配置 - 服务器端使用较严格的参数
    config->signatureGenerationConfig_.minFreqDelta = 80;   // 较大的最小频率差，更高的区分度
    config->signatureGenerationConfig_.maxFreqDelta = 800;  // 较大的最大频率差，宽广的匹配范围
    config->signatureGenerationConfig_.maxTimeDelta = 0.4;  // 较宽松的最大时间差，减少因速度变化导致的错误
    config->signatureGenerationConfig_.frameDuration = 0.25; // 服务器端使用较长的长帧时长，优化准确率
    
    // 匹配配置 - 服务器端使用较宽松的参数
    config->matchingConfig_.maxCandidates = 100;           // 较多的候选结果
    config->matchingConfig_.matchExpireTime = 10.0;        // 较长的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.3;  // 较低的置信度阈值
    config->matchingConfig_.minMatchesRequired = 10;       // 较少的最小匹配点数
    config->matchingConfig_.offsetTolerance = 0.05;        // 较小的时间偏移容忍度
    
    return config;
}

} // namespace afp 
