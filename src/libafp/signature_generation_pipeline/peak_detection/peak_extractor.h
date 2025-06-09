#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "frequency_band_manager.h"
#include <vector>
#include <algorithm>
#include "base/fft_result.h"
#include "base/peek.h"

namespace afp {

class PeakExtractor {
public:
    PeakExtractor(SignatureGenerationPipelineCtx* ctx);
    
    // 从FFT结果中提取峰值
    std::vector<Peak> extractPeaks(
        const std::vector<FFTResult>& fft_results,
        int start_idx, int end_idx,
        float quantile_threshold);

private:
    SignatureGenerationPipelineCtx* ctx_;
    
    // 检查是否为时频域局部最大值
    bool isLocalMaximum(
        const std::vector<FFTResult>& fft_results,
        int frame_idx, size_t freq_idx,
        float current_magnitude) const;
    
    // 计算分位数阈值
    float calculateQuantileThreshold(
        const std::vector<FFTResult>& fft_results,
        int start_idx, int end_idx,
        float quantile) const;
};

} // namespace afp 