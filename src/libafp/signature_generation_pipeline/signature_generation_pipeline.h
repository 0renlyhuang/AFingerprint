#pragma once

// 诊断日志控制宏
// 如需启用详细的诊断日志来调试指纹生成管道，请在编译时定义 ENABLED_DIAGNOSE 宏
// 例如：g++ -DENABLED_DIAGNOSE ...
// 或者在此处取消注释以下行：
// #define ENABLED_DIAGNOSE

#include "signature_generation_pipeline/phase/channel_split_phase.h"
#include "signature_generation_pipeline/phase/emphasis_phase.h"
#include "signature_generation_pipeline/phase/fft_phase.h"
#include "signature_generation_pipeline/phase/peak_detection_phase.h"
#include "signature_generation_pipeline/phase/long_frame_building_phase.h"
#include "signature_generation_pipeline/phase/hash_computation_phase.h"
#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include <memory>

namespace afp {

class SignatureGenerationPipeline {

public:
    SignatureGenerationPipeline(std::shared_ptr<IPerformanceConfig> config, std::shared_ptr<PCMFormat> format, SignaturePointsGeneratedCallback&& on_signature_points_generated);

    SignatureGenerationPipeline(const SignatureGenerationPipeline&) = delete;
    SignatureGenerationPipeline& operator=(const SignatureGenerationPipeline&) = delete;

    ~SignatureGenerationPipeline() = default;

    bool appendStreamBuffer(const void* buffer, 
                        size_t bufferSize,
                        double startTimestamp);

    void attachVisualizationConfig(VisualizationConfig* visualization_config);

private:
    SignatureGenerationPipelineCtx ctx_;

    ChannelSplitPhase channelSplitPhase_;
    EmphasisPhase emphasisPhase_;
    FftPhase fftPhase_;
    PeakDetectionPhase peakDetectionPhase_;
    LongFrameBuildingPhase longFrameBuildingPhase_;
    HashComputationPhase hashComputationPhase_;
};

} // namespace afp