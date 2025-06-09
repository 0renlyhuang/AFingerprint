#pragma once

#include "config/performance_config.h"
#include <memory>
#include <functional>
#include <vector>
#include "base/channel_array.h"
#include "afp/isignature_generator.h"
#include "afp/pcm_format.h"
#include "base/visualization_config.h"

namespace afp {

using SignaturePointsGeneratedCallback = std::function<void(const std::vector<SignaturePoint>&)>;

struct SignatureGenerationPipelineCtx {
    std::shared_ptr<IPerformanceConfig> config;
    std::shared_ptr<PCMFormat> format;

    ChannelArray<float *> channel_samples;
    size_t channel_buffer_sample_count;  // 每个通道的样本数量
    size_t fft_size;

    size_t channel_count;
    uint32_t sample_rate;

    SignaturePointsGeneratedCallback on_signature_points_generated;

    VisualizationConfig* visualization_config;

    SignatureGenerationPipelineCtx(std::shared_ptr<IPerformanceConfig> a_config, std::shared_ptr<PCMFormat> a_format, SignaturePointsGeneratedCallback&& a_on_signature_points_generated) 
    : config(a_config)
    , format(a_format)
    , channel_buffer_sample_count(a_config->getFFTConfig().fftSize)  // 每个通道的缓冲区大小是FFT Size
    , channel_count(a_format->channels())
    , sample_rate(a_format->sampleRate())
    , on_signature_points_generated(std::move(a_on_signature_points_generated))
    , fft_size(a_config->getFFTConfig().fftSize)
    {
        channel_samples.fill(nullptr);
        for (size_t i = 0; i < channel_count; i++) {
            channel_samples[i] = new float[channel_buffer_sample_count];
        }
    }

    SignatureGenerationPipelineCtx(const SignatureGenerationPipelineCtx&) = delete;
    SignatureGenerationPipelineCtx& operator=(const SignatureGenerationPipelineCtx&) = delete;

    ~SignatureGenerationPipelineCtx() {
        for (size_t i = 0; i < channel_count; i++) {
            delete[] channel_samples[i];
        }
    }
};

} // namespace afp