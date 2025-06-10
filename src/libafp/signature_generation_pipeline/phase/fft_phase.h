#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "signature_generation_pipeline/phase/peak_detection_phase.h"
#include "base/channel_array.h"
#include "base/ring_buffer.h"
#include "base/fft_result.h"
#include "fft/fft_interface.h"

namespace afp {

class FftPhase {

public:
    FftPhase(SignatureGenerationPipelineCtx* ctx);

    ~FftPhase();
    
    void attach(PeakDetectionPhase* peakDetectionPhase);


    void handleSamples(ChannelArray<float*>& channel_samples, size_t sample_count, double start_timestamp);

    void flush(ChannelArray<float*>& channel_samples, size_t sample_count);

private:
    void handleSamplesImpl(ChannelArray<float*>& channel_samples, size_t sample_count);

    // Process one FFT window from ring buffer
    void processFFTWindow(size_t channel_i, double timestamp);
private:
    SignatureGenerationPipelineCtx* ctx_;
    PeakDetectionPhase* peakDetectionPhase_;

    const size_t fft_size_;
    std::vector<float> hanning_window_;
    std::vector<float> windowed_samples_;
    std::unique_ptr<FFTInterface> fft_;
    std::vector<std::complex<float>> fft_result_buffer_;  // Complex buffer for FFT output
    const size_t hop_size_;
    
    // Ring buffer for overlapping windows
    ChannelArray<RingBufferPtr<float>> ring_buffers_;
    
    // Storage for FFT results
    ChannelArray<std::vector<FFTResult>> fft_results_;

    double current_timestamp_ = 0.0;
    bool has_current_timestamp_ = false;
    


};

} // namespace afp