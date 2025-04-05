#include "config/performance_config.h"
#include <map>

namespace afp {

// 静态配置实例缓存
static std::map<PlatformType, std::shared_ptr<PerformanceConfig>> configCache;

std::shared_ptr<PerformanceConfig> PerformanceConfig::getConfig(PlatformType platform) {
    // 如果缓存中没有，创建新的配置
    if (configCache.find(platform) == configCache.end()) {
        switch (platform) {
            case PlatformType::Mobile:
                configCache[platform] = createMobileConfig();
                break;
            case PlatformType::Desktop:
                configCache[platform] = createDesktopConfig();
                break;
            case PlatformType::Server:
                configCache[platform] = createServerConfig();
                break;
        }
    }
    return configCache[platform];
}

std::shared_ptr<PerformanceConfig> PerformanceConfig::createMobileConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 移动端使用较小的窗口以节省内存和计算资源
    config->fftConfig_.fftSize = 1024;    // 较小的FFT窗口
    config->fftConfig_.hopSize = 256;     // 较大的帧移以减少计算量
    
    // 峰值检测配置 - 移动端使用较宽松的参数
    config->peakDetectionConfig_.localMaxRange = 2;        // 较小的本地最大值范围
    config->peakDetectionConfig_.maxPeaksPerFrame = 5;     // 每帧保留较少的峰值
    config->peakDetectionConfig_.minPeakMagnitude = 0.1f;  // 较低的峰值幅度阈值
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5000;           // 最大频率
    
    // 指纹生成配置 - 移动端使用较宽松的参数
    config->signatureGenerationConfig_.minFreqDelta = 20;   // 较小的最小频率差
    config->signatureGenerationConfig_.maxFreqDelta = 400;  // 较大的最大频率差
    config->signatureGenerationConfig_.maxTimeDelta = 2.0;  // 较短的最大时间差
    
    // 匹配配置 - 移动端使用较严格的参数以减少内存使用
    config->matchingConfig_.maxCandidates = 20;            // 较少的候选结果
    config->matchingConfig_.matchExpireTime = 3.0;         // 较短的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.5;  // 较高的置信度阈值
    config->matchingConfig_.minMatchesRequired = 20;       // 较多的最小匹配点数
    config->matchingConfig_.offsetTolerance = 0.15;        // 较大的时间偏移容忍度
    
    return config;
}

std::shared_ptr<PerformanceConfig> PerformanceConfig::createDesktopConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - PC端使用中等大小的窗口
    config->fftConfig_.fftSize = 2048;    // 中等FFT窗口
    config->fftConfig_.hopSize = 512;     // 中等帧移
    
    // 峰值检测配置 - PC端使用中等参数
    config->peakDetectionConfig_.localMaxRange = 3;        // 中等本地最大值范围
    config->peakDetectionConfig_.maxPeaksPerFrame = 10;    // 每帧保留中等数量的峰值
    config->peakDetectionConfig_.minPeakMagnitude = 0.05f; // 中等峰值幅度阈值
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5500;           // 最大频率
    
    // 指纹生成配置 - PC端使用中等参数
    config->signatureGenerationConfig_.minFreqDelta = 30;   // 中等最小频率差
    config->signatureGenerationConfig_.maxFreqDelta = 300;  // 中等最大频率差
    config->signatureGenerationConfig_.maxTimeDelta = 3.0;  // 中等最大时间差
    
    // 匹配配置 - PC端使用中等参数
    config->matchingConfig_.maxCandidates = 50;            // 中等候选结果数
    config->matchingConfig_.matchExpireTime = 5.0;         // 中等过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.4;  // 中等置信度阈值
    config->matchingConfig_.minMatchesRequired = 15;       // 中等最小匹配点数
    config->matchingConfig_.offsetTolerance = 0.1;         // 中等时间偏移容忍度
    
    return config;
}

std::shared_ptr<PerformanceConfig> PerformanceConfig::createServerConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 服务器端使用较大的窗口以获得更好的频率分辨率
    config->fftConfig_.fftSize = 4096;    // 较大的FFT窗口
    config->fftConfig_.hopSize = 1024;    // 较大的帧移
    
    // 峰值检测配置 - 服务器端使用较严格的参数
    config->peakDetectionConfig_.localMaxRange = 4;        // 较大的本地最大值范围
    config->peakDetectionConfig_.maxPeaksPerFrame = 15;    // 每帧保留较多的峰值
    config->peakDetectionConfig_.minPeakMagnitude = 0.02f; // 较低的峰值幅度阈值
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 6000;           // 最大频率
    
    // 指纹生成配置 - 服务器端使用较严格的参数
    config->signatureGenerationConfig_.minFreqDelta = 40;   // 较大的最小频率差
    config->signatureGenerationConfig_.maxFreqDelta = 250;  // 较小的最大频率差
    config->signatureGenerationConfig_.maxTimeDelta = 4.0;  // 较大的最大时间差
    
    // 匹配配置 - 服务器端使用较宽松的参数
    config->matchingConfig_.maxCandidates = 100;           // 较多的候选结果
    config->matchingConfig_.matchExpireTime = 10.0;        // 较长的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.3;  // 较低的置信度阈值
    config->matchingConfig_.minMatchesRequired = 10;       // 较少的最小匹配点数
    config->matchingConfig_.offsetTolerance = 0.05;        // 较小的时间偏移容忍度
    
    return config;
}

} // namespace afp 