#pragma once

#include "base/ring_buffer.h"
#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "signature_generation_pipeline/phase/hash_computation_phase.h"
#include "base/channel_array.h"

namespace afp {

class LongFrameBuildingPhase {

public:
    LongFrameBuildingPhase(SignatureGenerationPipelineCtx* ctx);

    ~LongFrameBuildingPhase();

    void attach(HashComputationPhase* hash_computation_phase);

    void handlePeaks(ChannelArray<std::vector<Peak>>& peaks);

    void flushPeaks();

private:
    struct WndInfo {
        double start_time;
        double end_time;
    };

    void handleChannelPeaks(size_t channel, std::vector<Peak>& peaks);

    void consumePeaks(size_t channel);

private:
    SignatureGenerationPipelineCtx* ctx_;
    size_t max_peak_count_;
    ChannelArray<std::vector<Peak>> peak_buffers_;

    ChannelArray<WndInfo> wnd_infos_;

    ChannelArray<std::vector<Frame>> long_frames_;

    HashComputationPhase* hash_computation_phase_;
};

} // namespace afp