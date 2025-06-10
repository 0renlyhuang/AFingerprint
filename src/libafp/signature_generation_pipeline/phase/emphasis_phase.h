#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "signature_generation_pipeline/phase/fft_phase.h"
#include "base/channel_array.h"

namespace afp {

class EmphasisPhase {

public:
    EmphasisPhase(SignatureGenerationPipelineCtx* ctx);

    ~EmphasisPhase();

    void attach(FftPhase* fftPhase);

    void handleSamples(ChannelArray<float*>& channel_samples, size_t sample_count, double start_timestamp);

    void flush(ChannelArray<float*>& channel_samples, size_t sample_count);

private:
    SignatureGenerationPipelineCtx* ctx_;
    FftPhase* fftPhase_;
};

} // namespace afp