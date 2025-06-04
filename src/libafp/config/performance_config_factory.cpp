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
        case PlatformType::Mobile_Gen:
            return createMobileGenConfig();
        case PlatformType::Desktop_Gen:
            return createDesktopGenConfig();
        case PlatformType::Server_Gen:
            return createServerGenConfig();
        default:
            return createDesktopConfig();
    }
}

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createMobileConfig() {
     auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 移动端使用较小的窗口以节省内存和计算资源
    config->fftConfig_.fftSize = 4096;    // 较小的FFT窗口
    config->fftConfig_.hopSize = 441;     // 0.01秒/帧 (44.1kHz采样率下约为441样本)
    
    // 峰值检测配置 - 针对每帧3-5个峰值的要求优化
    config->peakDetectionConfig_.localMaxRange = 5;        // 较小的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 4;         // 默认为1，只与相邻帧比较
    config->peakDetectionConfig_.minPeakMagnitude = 25.0f;  // 提高阈值到45dB，有效过滤噪声
    config->peakDetectionConfig_.minFreq = 40;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5000;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.1;   // 移动端使用较小的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.75f; // 移动端使用75分位数，平衡性能和准确性
    config->peakDetectionConfig_.numFrequencyBands = 6;    // 移动端使用4个频段，平衡性能和准确性
    
    // 动态峰值分配配置 - 移动端
    config->peakDetectionConfig_.minPeaksPerFrame = 5;      // 最少保留3个峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 15; // 最多保留15个峰值
    config->peakDetectionConfig_.noiseEstimationWindow = 2.0; // 2秒噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 6.0f;       // 6dB信噪比阈值
    config->peakDetectionConfig_.energyWeightFactor = 0.8f; // 能量权重60%
    config->peakDetectionConfig_.snrWeightFactor = 0.2f;    // 信噪比权重40%
    
    // 指纹生成配置 - 针对三帧组合哈希优化
    config->signatureGenerationConfig_.minFreqDelta = 60;   // 最小频率差，增加区分度
    config->signatureGenerationConfig_.maxFreqDelta = 3500;  // 最大频率差，避免跨度太大
    config->signatureGenerationConfig_.maxTimeDelta = 0.3;  // 最大时间差限制为0.2秒，增强时间相关性
    config->signatureGenerationConfig_.frameDuration = 0.08; // 移动端使用较短的长帧时长，优化性能
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 7; // 移动端保留8个最佳组合，平衡性能和准确性
    config->signatureGenerationConfig_.minDoubleFrameScore = 10.0; // 移动端评分阈值，过滤质量较差的组合
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 7; // 移动端保留5个最佳三帧组合，优化性能
    config->signatureGenerationConfig_.minTripleFrameScore = 15.0; // 移动端三帧评分阈值，过滤低质量组合
    
    // 扩展三帧选取配置 - 移动端
    config->signatureGenerationConfig_.symmetricFrameRange = 2;    // 移动端对称范围2，生成(x-2,x,x+2)到(x-1,x,x+1)
    
    // 匹配配置 - 移动端使用较严格的参数以减少内存使用
    config->matchingConfig_.maxCandidates = 200;            // 较少的候选结果
    config->matchingConfig_.maxCandidatesPerSignature = 25;  // 移动端每个signature最多3个候选结果，节省内存
    config->matchingConfig_.matchExpireTime = 5.0;         // 较短的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.5;  // 较高的置信度阈值
    config->matchingConfig_.minMatchesRequired = 5;       // 减少最小匹配点数要求
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 3; // 移动端至少3个不同时间戳
    config->matchingConfig_.offsetTolerance = 0.1;        // 较大的时间偏移容忍度
    
    return config;
}


std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createMobileGenConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 生成模式使用更大的窗口以获得更好的频率分辨率
    config->fftConfig_.fftSize = 4096;    // 增大FFT窗口提高频率分辨率
    config->fftConfig_.hopSize = 441;     // 更密集的分析
    
    // 峰值检测配置 - 生成模式优先精度，使用更严格的参数
    config->peakDetectionConfig_.localMaxRange = 4;        // 更大的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 3;         // 更大的时间维度范围
    config->peakDetectionConfig_.minPeakMagnitude = 15.0f;  // 降低阈值以捕获更多细节
    config->peakDetectionConfig_.minFreq = 40;            // 保持最小频率
    config->peakDetectionConfig_.maxFreq = 5000;           // 扩大频率范围
    config->peakDetectionConfig_.peakTimeDuration = 0.1;   // 增大峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.5f; // 提高到90分位数，确保高质量峰值
    config->peakDetectionConfig_.numFrequencyBands = 8;    // 增加频段数提高精度
    
    // 动态峰值分配配置 - 生成模式保留更多峰值
    config->peakDetectionConfig_.minPeaksPerFrame = 20;      // 保留更多峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 30; // 提高峰值上限
    config->peakDetectionConfig_.noiseEstimationWindow = 3.0; // 更长的噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 4.0f;       // 降低信噪比阈值，保留更多信息
    config->peakDetectionConfig_.energyWeightFactor = 0.8f; // 更注重能量
    config->peakDetectionConfig_.snrWeightFactor = 0.2f;    // 适当考虑信噪比
    
    // 指纹生成配置 - 生成模式追求更高精度和覆盖率
    config->signatureGenerationConfig_.minFreqDelta = 40;   // 降低最小频率差，增加密度
    config->signatureGenerationConfig_.maxFreqDelta = 4000;  // 扩大频率差范围
    config->signatureGenerationConfig_.maxTimeDelta = 0.3;  // 增大时间差允许范围
    config->signatureGenerationConfig_.frameDuration = 0.08; // 适中的帧时长
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 20; // 保留更多双帧组合
    config->signatureGenerationConfig_.minDoubleFrameScore = 5.0; // 降低评分阈值，保留更多组合
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 50; // 保留更多三帧组合
    config->signatureGenerationConfig_.minTripleFrameScore = 15.0; // 降低三帧评分阈值
    
    // 扩展三帧选取配置 - 生成模式使用更大范围
    config->signatureGenerationConfig_.symmetricFrameRange = 2;    // 扩大对称范围
    
    // 匹配配置 - 生成模式不直接用于匹配，但保持合理设置
    config->matchingConfig_.maxCandidates = 200;            // 较少的候选结果
    config->matchingConfig_.maxCandidatesPerSignature = 25;  // 移动端每个signature最多3个候选结果，节省内存
    config->matchingConfig_.matchExpireTime = 5.0;         // 较短的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.5;  // 较高的置信度阈值
    config->matchingConfig_.minMatchesRequired = 5;       // 减少最小匹配点数要求
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 3; // 移动端至少3个不同时间戳
    config->matchingConfig_.offsetTolerance = 0.1;        // 较大的时间偏移容忍度
    
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
    config->peakDetectionConfig_.minPeakMagnitude = 40.0f; // 提高阈值到40dB，平衡敏感度和抗噪性
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 5500;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.25;  // PC端使用中等峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.8f;  // 桌面端使用80分位数，提高峰值质量
    config->peakDetectionConfig_.numFrequencyBands = 5;    // 桌面端使用5个频段，提高精度
    
    // 动态峰值分配配置 - 桌面端
    config->peakDetectionConfig_.minPeaksPerFrame = 4;      // 最少保留4个峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 20; // 最多保留20个峰值
    config->peakDetectionConfig_.noiseEstimationWindow = 3.0; // 3秒噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 8.0f;       // 8dB信噪比阈值
    config->peakDetectionConfig_.energyWeightFactor = 0.5f; // 能量权重50%
    config->peakDetectionConfig_.snrWeightFactor = 0.5f;    // 信噪比权重50%
    
    // 指纹生成配置 - PC端使用中等参数
    config->signatureGenerationConfig_.minFreqDelta = 70;   // 中等最小频率差，增强区分度
    config->signatureGenerationConfig_.maxFreqDelta = 600;  // 中等最大频率差，允许更广范围的匹配
    config->signatureGenerationConfig_.maxTimeDelta = 0.3;  // 中等最大时间差，增强时序严谨性
    config->signatureGenerationConfig_.frameDuration = 0.18; // PC端使用中等长帧时长，平衡准确率和性能
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 15; // 桌面端保留15个最佳组合，提高准确性
    config->signatureGenerationConfig_.minDoubleFrameScore = 12.0; // 桌面端评分阈值，平衡准确性和覆盖率
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 10; // 桌面端保留10个最佳三帧组合，平衡准确性和性能
    config->signatureGenerationConfig_.minTripleFrameScore = 18.0; // 桌面端三帧评分阈值，平衡准确性和覆盖率
    
    // 扩展三帧选取配置 - 桌面端
    config->signatureGenerationConfig_.symmetricFrameRange = 3;    // 桌面端对称范围3，生成(x-3,x,x+3)到(x-1,x,x+1)
    
    // 匹配配置 - PC端使用中等参数
    config->matchingConfig_.maxCandidates = 50;            // 中等候选结果数
    config->matchingConfig_.maxCandidatesPerSignature = 5;  // 桌面端每个signature最多5个候选结果，平衡性能和准确性
    config->matchingConfig_.matchExpireTime = 5.0;         // 中等过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.4;  // 中等置信度阈值
    config->matchingConfig_.minMatchesRequired = 15;       // 中等最小匹配点数
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 8; // 桌面端至少8个不同时间戳
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
    config->peakDetectionConfig_.minPeakMagnitude = 35.0f; // 设置为35dB，服务器端更敏感以捕获更多细节
    config->peakDetectionConfig_.minFreq = 250;            // 最小频率
    config->peakDetectionConfig_.maxFreq = 6000;           // 最大频率
    config->peakDetectionConfig_.peakTimeDuration = 0.3;   // 服务器端使用较大的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.85f; // 服务器端使用85分位数，最高峰值质量
    config->peakDetectionConfig_.numFrequencyBands = 6;    // 服务器端使用6个频段，最高精度
    
    // 动态峰值分配配置 - 服务器端
    config->peakDetectionConfig_.minPeaksPerFrame = 5;      // 最少保留5个峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 25; // 最多保留25个峰值
    config->peakDetectionConfig_.noiseEstimationWindow = 5.0; // 5秒噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 10.0f;      // 10dB信噪比阈值
    config->peakDetectionConfig_.energyWeightFactor = 0.4f; // 能量权重40%
    config->peakDetectionConfig_.snrWeightFactor = 0.6f;    // 信噪比权重60%
    
    // 指纹生成配置 - 服务器端使用较严格的参数
    config->signatureGenerationConfig_.minFreqDelta = 80;   // 较大的最小频率差，更高的区分度
    config->signatureGenerationConfig_.maxFreqDelta = 800;  // 较大的最大频率差，宽广的匹配范围
    config->signatureGenerationConfig_.maxTimeDelta = 0.4;  // 较宽松的最大时间差，减少因速度变化导致的错误
    config->signatureGenerationConfig_.frameDuration = 0.25; // 服务器端使用较长的长帧时长，优化准确率
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 25; // 服务器端保留25个最佳组合，最高准确性
    config->signatureGenerationConfig_.minDoubleFrameScore = 10.0; // 服务器端评分阈值，更宽松以获得更高覆盖率
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 15; // 服务器端保留15个最佳三帧组合，最高准确性
    config->signatureGenerationConfig_.minTripleFrameScore = 12.0; // 服务器端三帧评分阈值，更宽松以获得更高覆盖率
    
    // 扩展三帧选取配置 - 服务器端
    config->signatureGenerationConfig_.symmetricFrameRange = 4;    // 服务器端对称范围4，生成(x-4,x,x+4)到(x-1,x,x+1)
    
    // 匹配配置 - 服务器端使用较宽松的参数
    config->matchingConfig_.maxCandidates = 100;           // 较多的候选结果
    config->matchingConfig_.maxCandidatesPerSignature = 10; // 服务器端每个signature最多10个候选结果，追求最高准确性
    config->matchingConfig_.matchExpireTime = 10.0;        // 较长的过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.3;  // 较低的置信度阈值
    config->matchingConfig_.minMatchesRequired = 10;       // 较少的最小匹配点数
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 6; // 服务器端至少6个不同时间戳
    config->matchingConfig_.offsetTolerance = 0.05;        // 较小的时间偏移容忍度
    
    return config;
}


std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createDesktopGenConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 生成模式使用更大的窗口
    config->fftConfig_.fftSize = 4096;    // 大FFT窗口提高分辨率
    config->fftConfig_.hopSize = 1024;    // 更密集的分析
    
    // 峰值检测配置 - 桌面生成模式优先精度
    config->peakDetectionConfig_.localMaxRange = 7;        // 更大的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 6;         // 更大的时间维度范围
    config->peakDetectionConfig_.minPeakMagnitude = 30.0f; // 适中的阈值平衡精度和覆盖率
    config->peakDetectionConfig_.minFreq = 200;            // 保持最小频率
    config->peakDetectionConfig_.maxFreq = 6000;           // 扩大频率范围
    config->peakDetectionConfig_.peakTimeDuration = 0.35;  // 更大的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.9f;  // 高质量峰值
    config->peakDetectionConfig_.numFrequencyBands = 8;    // 增加频段数
    
    // 动态峰值分配配置 - 桌面生成模式
    config->peakDetectionConfig_.minPeaksPerFrame = 10;     // 保留更多峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 35; // 更高峰值上限
    config->peakDetectionConfig_.noiseEstimationWindow = 4.0; // 更长的噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 6.0f;       // 适中的信噪比阈值
    config->peakDetectionConfig_.energyWeightFactor = 0.5f; // 平衡能量和信噪比
    config->peakDetectionConfig_.snrWeightFactor = 0.5f;    // 平衡权重
    
    // 指纹生成配置 - 桌面生成模式追求高精度
    config->signatureGenerationConfig_.minFreqDelta = 50;   // 适中的最小频率差
    config->signatureGenerationConfig_.maxFreqDelta = 700;  // 扩大频率差范围
    config->signatureGenerationConfig_.maxTimeDelta = 0.4;  // 增大时间差范围
    config->signatureGenerationConfig_.frameDuration = 0.22; // 适中的帧时长
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 30; // 保留更多双帧组合
    config->signatureGenerationConfig_.minDoubleFrameScore = 6.0; // 降低评分阈值
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 20; // 保留更多三帧组合
    config->signatureGenerationConfig_.minTripleFrameScore = 10.0; // 适中的三帧评分阈值
    
    // 扩展三帧选取配置 - 桌面生成模式
    config->signatureGenerationConfig_.symmetricFrameRange = 5;    // 更大的对称范围
    
    // 匹配配置 - 桌面生成模式设置
    config->matchingConfig_.maxCandidates = 100;           // 较多候选结果
    config->matchingConfig_.maxCandidatesPerSignature = 15; // 每个signature更多候选
    config->matchingConfig_.matchExpireTime = 8.0;         // 较长过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.35; // 稍低的置信度阈值
    config->matchingConfig_.minMatchesRequired = 12;       // 适中的最小匹配点数
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 6; // 适中的唯一时间戳要求
    config->matchingConfig_.offsetTolerance = 0.08;        // 适中的时间偏移容忍度
    
    return config;
}

std::shared_ptr<IPerformanceConfig> PerformanceConfigFactory::createServerGenConfig() {
    auto config = std::unique_ptr<PerformanceConfig>(new PerformanceConfig());
    
    // FFT配置 - 服务器生成模式使用最大窗口获得最佳分辨率
    config->fftConfig_.fftSize = 8192;    // 最大FFT窗口
    config->fftConfig_.hopSize = 2048;    // 非常密集的分析
    
    // 峰值检测配置 - 服务器生成模式追求最高精度
    config->peakDetectionConfig_.localMaxRange = 8;        // 最大的本地最大值范围
    config->peakDetectionConfig_.timeMaxRange = 7;         // 最大的时间维度范围
    config->peakDetectionConfig_.minPeakMagnitude = 25.0f; // 较低阈值捕获更多细节
    config->peakDetectionConfig_.minFreq = 200;            // 保持最小频率
    config->peakDetectionConfig_.maxFreq = 7000;           // 最大频率范围
    config->peakDetectionConfig_.peakTimeDuration = 0.4;   // 最大的峰值检测窗口
    config->peakDetectionConfig_.quantileThreshold = 0.95f; // 最高质量峰值（95分位数）
    config->peakDetectionConfig_.numFrequencyBands = 10;   // 最多频段数
    
    // 动态峰值分配配置 - 服务器生成模式
    config->peakDetectionConfig_.minPeaksPerFrame = 12;     // 保留最多峰值
    config->peakDetectionConfig_.maxPeaksPerFrameLimit = 50; // 最高峰值上限
    config->peakDetectionConfig_.noiseEstimationWindow = 6.0; // 最长的噪声估计窗口
    config->peakDetectionConfig_.snrThreshold = 3.0f;       // 最低信噪比阈值，保留最多信息
    config->peakDetectionConfig_.energyWeightFactor = 0.3f; // 更注重信噪比质量
    config->peakDetectionConfig_.snrWeightFactor = 0.7f;    // 主要考虑信噪比
    
    // 指纹生成配置 - 服务器生成模式追求最高精度和覆盖率
    config->signatureGenerationConfig_.minFreqDelta = 30;   // 最小的频率差，最高密度
    config->signatureGenerationConfig_.maxFreqDelta = 1000; // 最大的频率差范围
    config->signatureGenerationConfig_.maxTimeDelta = 0.5;  // 最大的时间差范围
    config->signatureGenerationConfig_.frameDuration = 0.3;  // 较长的帧时长获得更好稳定性
    config->signatureGenerationConfig_.maxDoubleFrameCombinations = 50; // 保留最多双帧组合
    config->signatureGenerationConfig_.minDoubleFrameScore = 3.0; // 最低评分阈值，最大覆盖率
    config->signatureGenerationConfig_.maxTripleFrameCombinations = 30; // 保留最多三帧组合
    config->signatureGenerationConfig_.minTripleFrameScore = 5.0; // 最低三帧评分阈值
    
    // 扩展三帧选取配置 - 服务器生成模式
    config->signatureGenerationConfig_.symmetricFrameRange = 6;    // 最大的对称范围
    
    // 匹配配置 - 服务器生成模式设置
    config->matchingConfig_.maxCandidates = 150;           // 最多候选结果
    config->matchingConfig_.maxCandidatesPerSignature = 20; // 每个signature最多候选
    config->matchingConfig_.matchExpireTime = 12.0;        // 最长过期时间
    config->matchingConfig_.minConfidenceThreshold = 0.25; // 最低置信度阈值
    config->matchingConfig_.minMatchesRequired = 8;        // 较少的最小匹配点数
    config->matchingConfig_.minMatchesUniqueTimestampRequired = 4; // 较少的唯一时间戳要求
    config->matchingConfig_.offsetTolerance = 0.06;        // 更严格的时间偏移容忍度
    
    return config;
}

} // namespace afp 
