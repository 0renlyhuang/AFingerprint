#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include "iperformance_config.h"

namespace afp {

class PerformanceConfig : public IPerformanceConfig {
    friend class PerformanceConfigFactory;
public:
    // 获取各个配置
    const FFTConfig& getFFTConfig() const override { return fftConfig_; }
    const PeakDetectionConfig& getPeakDetectionConfig() const override { return peakDetectionConfig_; }
    const SignatureGenerationConfig& getSignatureGenerationConfig() const override { return signatureGenerationConfig_; }
    const MatchingConfig& getMatchingConfig() const override { return matchingConfig_; }

private:
    // 默认构造函数
    PerformanceConfig() = default;

    // 配置参数
    FFTConfig fftConfig_;
    PeakDetectionConfig peakDetectionConfig_;
    SignatureGenerationConfig signatureGenerationConfig_;
    MatchingConfig matchingConfig_;
};

} // namespace afp 