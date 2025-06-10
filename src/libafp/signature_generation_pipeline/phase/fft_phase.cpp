#include "fft_phase.h"
#include <cmath>
#include <iostream>

namespace afp {

FftPhase::FftPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx) 
    , fft_size_(ctx->config->getFFTConfig().fftSize)
    , hop_size_(ctx->config->getFFTConfig().hopSize)
    {
    // 初始化汉宁窗
    hanning_window_.resize(fft_size_);
    for (size_t i = 0; i < fft_size_; ++i) {
        hanning_window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fft_size_ - 1)));
    }

    windowed_samples_.resize(fft_size_);
    
    // 初始化FFT缓冲区
    fft_result_buffer_.resize(fft_size_);

    fft_ = FFTFactory::create(fft_size_);

    fft_results_.fill(std::vector<FFTResult>());
    for (size_t channel_i = 0; channel_i < ctx->channel_count; ++channel_i) {
        ring_buffers_[channel_i] = std::make_unique<RingBuffer<float>>(fft_size_);
        fft_results_[channel_i].reserve(fft_size_ / hop_size_);
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-FFT] FftPhase 初始化: FFT大小=" << fft_size_ 
              << ", hop大小=" << hop_size_ << ", 通道数=" << ctx->channel_count 
              << ", 采样率=" << ctx->sample_rate << "Hz" << std::endl;
    std::cout << "[DIAGNOSE-FFT] 窗函数设置完成，前5个汉宁窗系数: ";
    for (size_t i = 0; i < std::min<size_t>(5, fft_size_); ++i) {
        std::cout << hanning_window_[i] << " ";
    }
    std::cout << std::endl;
#endif
}

void FftPhase::attach(PeakDetectionPhase* peakDetectionPhase) {
    peakDetectionPhase_ = peakDetectionPhase;
}

FftPhase::~FftPhase() = default;

void FftPhase::handleSamples(ChannelArray<float*>& channel_samples, size_t sample_count, double start_timestamp) {
    if (!has_current_timestamp_) {
        current_timestamp_ = start_timestamp;
        has_current_timestamp_ = true;
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-FFT] 开始处理样本: 样本数=" << sample_count 
              << ", 起始时间=" << start_timestamp << "s" << std::endl;
#endif

    handleSamplesImpl(channel_samples, sample_count);
}

void FftPhase::handleSamplesImpl(ChannelArray<float*>& channel_samples, size_t sample_count) {
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        fft_results_[channel_i].clear();
    }

    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        float* samples = channel_samples[channel_i];
        size_t samples_remaining = sample_count;
        size_t sample_offset = 0;
        RingBuffer<float>* ring_buffer = ring_buffers_[channel_i].get();

        size_t fft_count_for_channel = 0;

#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-FFT] 处理通道" << channel_i << ", ring buffer状态: 当前大小=" 
                  << ring_buffer->size() << "/" << ring_buffer->capacity() << std::endl;
#endif
    
        while (samples_remaining > 0) {
            // 写入数据到ring buffer
            size_t samples_written = ring_buffer->write(samples + sample_offset, samples_remaining);
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "写入" << samples_written 
                      << "个样本到ring buffer, 新大小=" << ring_buffer->size() << std::endl;
#endif
            
            sample_offset += samples_written;
            samples_remaining -= samples_written;
            
            // 如果ring buffer已满，执行FFT处理
            if (ring_buffer->full()) {
                fft_count_for_channel++;
                
                // 计算窗口开始时间戳：当前时间戳减去窗口长度
                double window_start_timestamp = current_timestamp_;
                
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "ring buffer已满，执行第" 
                          << fft_count_for_channel << "次FFT" << std::endl;
                std::cout << "  窗口开始时间戳=" << window_start_timestamp << "s" << std::endl;
                std::cout << "  窗口长度=" << (static_cast<double>(hop_size_) / ctx_->sample_rate) << "s" << std::endl;
#endif
                
                processFFTWindow(channel_i, window_start_timestamp);
                
                // 移动窗口（移除hop_size_个样本）
                ring_buffer->moveWindow(hop_size_);
                
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "窗口移动" << hop_size_ 
                          << "个样本，新大小=" << ring_buffer->size() << std::endl;
#endif
                
                // 更新时间戳：移动hop_size_对应的时间
                current_timestamp_ += static_cast<double>(hop_size_) / ctx_->sample_rate;
            }
        }

#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "处理完成，共执行" << fft_count_for_channel 
                  << "次FFT，生成" << fft_results_[channel_i].size() << "个FFT结果" << std::endl;
#endif
    }

#ifdef ENABLED_DIAGNOSE
    size_t total_fft_results = 0;
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        total_fft_results += fft_results_[channel_i].size();
    }
    std::cout << "[DIAGNOSE-FFT] FFT处理完成，总共生成" << total_fft_results 
              << "个FFT结果，传递给峰值检测阶段, FFT结果详情:";
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        std::cout << "通道" << channel_i << ": " << fft_results_[channel_i].size() << "个FFT结果, 详情:[";
        for (const auto& fft_result : fft_results_[channel_i]) {
            std::cout << fft_result.timestamp << "s ";
        }
        std::cout << "]" << std::endl;
    }
#endif

    peakDetectionPhase_->handleShortFrames(fft_results_);
}


void FftPhase::processFFTWindow(size_t channel_i, double timestamp) {
    // 从ring buffer读取数据并应用窗函数
    ring_buffers_[channel_i]->read(windowed_samples_.data(), fft_size_);

#ifdef ENABLED_DIAGNOSE
    // 计算应用窗函数前的统计信息
    float pre_window_energy = 0.0f;
    for (size_t i = 0; i < fft_size_; ++i) {
        pre_window_energy += windowed_samples_[i] * windowed_samples_[i];
    }
    
    std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "FFT窗口处理: 窗口开始时间戳=" << timestamp 
              << "s, 窗函数前能量=" << pre_window_energy << std::endl;
#endif
    
    // 应用汉宁窗
    for (size_t i = 0; i < fft_size_; ++i) {
        windowed_samples_[i] *= hanning_window_[i];
    }

#ifdef ENABLED_DIAGNOSE
    // 计算应用窗函数后的统计信息
    float post_window_energy = 0.0f;
    for (size_t i = 0; i < fft_size_; ++i) {
        post_window_energy += windowed_samples_[i] * windowed_samples_[i];
    }
    
    // std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "应用汉宁窗后能量=" << post_window_energy 
    //           << ", 能量衰减比=" << (pre_window_energy > 0 ? post_window_energy / pre_window_energy : 0) << std::endl;
#endif
    
    // 执行FFT
    if (!fft_->transform(windowed_samples_.data(), fft_result_buffer_.data())) {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "FFT变换失败！" << std::endl;
#endif
        return;  // TODO: 错误处理
    }

    FFTResult fftResult;  // TTFResult 可以叫做ShortFrame
    fftResult.magnitudes.resize(fft_size_ / 2);
    fftResult.frequencies.resize(fft_size_ / 2);
    fftResult.timestamp = timestamp;

#ifdef ENABLED_DIAGNOSE
    float max_magnitude = 0.0f;
    float total_magnitude = 0.0f;
    size_t valid_bins = 0;
#endif
    
    // 计算幅度谱和频率
    for (size_t i = 0; i < fft_size_ / 2; ++i) {
        // 计算复数的模
        float magnitude = std::abs(fft_result_buffer_[i]);
        
        // 对数频谱，保持绝对值以确保不同短帧之间的可比性
        fftResult.magnitudes[i] = magnitude > 0.00001f ? 20.0f * std::log10(magnitude) + 100.0f : 0;
        
        // 计算每个bin对应的频率
        fftResult.frequencies[i] = i * static_cast<float>(ctx_->format->sampleRate()) / static_cast<float>(fft_size_);

#ifdef ENABLED_DIAGNOSE
        if (fftResult.magnitudes[i] > 0) {
            max_magnitude = std::max(max_magnitude, fftResult.magnitudes[i]);
            total_magnitude += fftResult.magnitudes[i];
            valid_bins++;
        }
#endif
    }

#ifdef ENABLED_DIAGNOSE
    // float avg_magnitude = (valid_bins > 0) ? total_magnitude / valid_bins : 0;
    // std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "FFT结果统计: 最大幅度=" << max_magnitude 
    //           << ", 平均幅度=" << avg_magnitude << ", 有效频率bins=" << valid_bins 
    //           << "/" << (fft_size_ / 2) << std::endl;
              
    // // 输出一些关键频率的幅度
    // std::cout << "[DIAGNOSE-FFT] 通道" << channel_i << "关键频率幅度: ";
    // std::vector<size_t> key_freqs = {100, 440, 1000, 2000, 4000}; // Hz
    // for (size_t freq : key_freqs) {
    //     size_t bin = freq * fft_size_ / ctx_->format->sampleRate();
    //     if (bin < fft_size_ / 2) {
    //         std::cout << freq << "Hz(" << fftResult.magnitudes[bin] << ") ";
    //     }
    // }
    // std::cout << std::endl;
#endif
    
    // 将FFT结果存储到vector中
    fft_results_[channel_i].push_back(std::move(fftResult));
}

void FftPhase::flush(ChannelArray<float*>& channel_samples, size_t sample_count) {
    handleSamplesImpl(channel_samples, sample_count);

    peakDetectionPhase_->flush();
}

} // namespace afp