#include "signature_generation_pipeline/signature_generation_pipeline.h"

namespace afp {

SignatureGenerationPipeline::SignatureGenerationPipeline(std::shared_ptr<IPerformanceConfig> config, std::shared_ptr<PCMFormat> format, SignaturePointsGeneratedCallback&& on_signature_points_generated)
    : ctx_(config, format, std::move(on_signature_points_generated))
    , channelSplitPhase_(&ctx_)
    , emphasisPhase_(&ctx_)
    , fftPhase_(&ctx_)
    , peakDetectionPhase_(&ctx_)
    , longFrameBuildingPhase_(&ctx_)
    , hashComputationPhase_(&ctx_)
    {
        // Wire
        channelSplitPhase_.attach(&emphasisPhase_);
        emphasisPhase_.attach(&fftPhase_);
        fftPhase_.attach(&peakDetectionPhase_);
        peakDetectionPhase_.attach(&longFrameBuildingPhase_);
        longFrameBuildingPhase_.attach(&hashComputationPhase_);

    }


bool SignatureGenerationPipeline::appendStreamBuffer(const void* buffer, size_t bufferSize, double startTimestamp) {
    channelSplitPhase_.handleAudioData(buffer, bufferSize, startTimestamp);
    
    return true;
}

void SignatureGenerationPipeline::flush() {
    channelSplitPhase_.flush();
}

void SignatureGenerationPipeline::attachVisualizationConfig(VisualizationConfig* visualization_config) {
    ctx_.visualization_config = visualization_config;
}

} // namespace afp