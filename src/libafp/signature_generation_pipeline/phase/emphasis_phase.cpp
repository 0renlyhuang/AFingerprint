#include "signature_generation_pipeline/phase/emphasis_phase.h"
#include <iostream>

namespace afp {

EmphasisPhase::EmphasisPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx) {
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-预加重] EmphasisPhase 初始化: 通道数=" << ctx->channel_count << std::endl;
#endif
}

EmphasisPhase::~EmphasisPhase() = default;

void EmphasisPhase::attach(FftPhase* fftPhase) {
    fftPhase_ = fftPhase;
}

void EmphasisPhase::handleSamples(ChannelArray<float*>& channel_samples, size_t sample_count, double start_timestamp) {
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-预加重] 开始处理样本: 样本数=" << sample_count 
              << ", 起始时间=" << start_timestamp << "s, 通道数=" << ctx_->channel_count << std::endl;
#endif

    // 对输入的buffer按FFT Size进行切分，一旦积累到FFT Size的buffer，就进行预加重处理，处理之后传入下一个阶段
    // 预加重处理：buffer[i] -= 0.95f * buffer[i-1];
    // 预加重处理之后，传入下一个阶段

    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        float* channel_sample = channel_samples[channel_i];

#ifdef ENABLED_DIAGNOSE
        // // 记录处理前的统计信息
        // float pre_min = channel_sample[0], pre_max = channel_sample[0];
        // double pre_sum = 0.0, pre_energy = 0.0;
        // for (size_t i = 0; i < sample_count; ++i) {
        //     pre_min = std::min(pre_min, channel_sample[i]);
        //     pre_max = std::max(pre_max, channel_sample[i]);
        //     pre_sum += channel_sample[i];
        //     pre_energy += channel_sample[i] * channel_sample[i];
        // }
        // float pre_mean = pre_sum / sample_count;
        // float pre_rms = std::sqrt(pre_energy / sample_count);
        
        // std::cout << "[DIAGNOSE-预加重] 通道" << channel_i << "处理前统计: 最小值=" << pre_min 
        //           << ", 最大值=" << pre_max << ", 平均值=" << pre_mean << ", RMS=" << pre_rms << std::endl;
        
        // // 输出前5个样本
        // std::cout << "[DIAGNOSE-预加重] 通道" << channel_i << "处理前前5个样本: ";
        // for (size_t s = 0; s < std::min<size_t>(5, sample_count); ++s) {
        //     std::cout << channel_sample[s] << " ";
        // }
        // std::cout << std::endl;
#endif

        for (size_t i = 1; i < sample_count; ++i) {
            channel_sample[i] -= 0.95f * channel_sample[i-1];
        }

#ifdef ENABLED_DIAGNOSE
        // // 记录处理后的统计信息
        // float post_min = channel_sample[0], post_max = channel_sample[0];
        // double post_sum = 0.0, post_energy = 0.0;
        // for (size_t i = 0; i < sample_count; ++i) {
        //     post_min = std::min(post_min, channel_sample[i]);
        //     post_max = std::max(post_max, channel_sample[i]);
        //     post_sum += channel_sample[i];
        //     post_energy += channel_sample[i] * channel_sample[i];
        // }
        // float post_mean = post_sum / sample_count;
        // float post_rms = std::sqrt(post_energy / sample_count);
        
        // std::cout << "[DIAGNOSE-预加重] 通道" << channel_i << "处理后统计: 最小值=" << post_min 
        //           << ", 最大值=" << post_max << ", 平均值=" << post_mean << ", RMS=" << post_rms << std::endl;
        
        // // 输出前5个样本
        // std::cout << "[DIAGNOSE-预加重] 通道" << channel_i << "处理后前5个样本: ";
        // for (size_t s = 0; s < std::min<size_t>(5, sample_count); ++s) {
        //     std::cout << channel_sample[s] << " ";
        // }
        // std::cout << std::endl;
        
        // // 计算预加重效果
        // float energy_ratio = (pre_energy > 0) ? (post_energy / pre_energy) : 0;
        // std::cout << "[DIAGNOSE-预加重] 通道" << channel_i << "预加重效果: 能量比=" << energy_ratio << std::endl;
#endif
    }

#ifdef ENABLED_DIAGNOSE
    // std::cout << "[DIAGNOSE-预加重] 预加重处理完成，传递给FFT阶段" << std::endl;
#endif

    fftPhase_->handleSamples(channel_samples, sample_count, start_timestamp);
}

void EmphasisPhase::flush(ChannelArray<float*>& channel_samples, size_t sample_count) {
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        float* channel_sample = channel_samples[channel_i];
        for (size_t i = 0; i < sample_count; ++i) {
            channel_sample[i] -= 0.95f * channel_sample[i-1];
        }
    }

    fftPhase_->flush(channel_samples, sample_count);
}

} // namespace afp