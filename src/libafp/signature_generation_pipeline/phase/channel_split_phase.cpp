#include "channel_split_phase.h"
#include <iostream>

namespace afp {

ChannelSplitPhase::ChannelSplitPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx), pcmReader_(*(ctx->format)) {
    // 初始化每个通道的写入位置为0
    channelWritePositions_.fill(0);
    channel_buffer_max_capacitys_.fill(ctx->channel_buffer_sample_count);
    src_last_consumed_bytes_counts_.fill(0);

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-通道分离] ChannelSplitPhase 初始化: 通道数=" << ctx->channel_count 
              << ", 采样率=" << ctx->sample_rate << "Hz, 每通道缓冲区大小=" 
              << ctx->channel_buffer_sample_count << "样本" << std::endl;
#endif
}

ChannelSplitPhase::~ChannelSplitPhase() = default;

void ChannelSplitPhase::attach(EmphasisPhase* emphasisPhase) {
    emphasisPhase_ = emphasisPhase;
}

void ChannelSplitPhase::handleAudioData(const void* buffer, size_t bufferSize, double startTimestamp) {
    if (!buffer || bufferSize == 0 || !emphasisPhase_) {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-通道分离] handleAudioData 无效输入: buffer=" << (buffer ? "有效" : "空") 
                  << ", bufferSize=" << bufferSize << ", emphasisPhase=" 
                  << (emphasisPhase_ ? "已连接" : "未连接") << std::endl;
#endif
        return;
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-通道分离] 开始处理音频数据: 大小=" << bufferSize << "字节, 起始时间=" 
              << startTimestamp << "s, 帧大小=" << ctx_->format->frameSize() << "字节" << std::endl;
#endif
    
    const uint8_t* srcData = static_cast<const uint8_t*>(buffer);
    size_t srcBytesRemaining = bufferSize;
    size_t src_data_offset = 0;
    double currentTimestamp = startTimestamp;

    src_last_consumed_bytes_counts_.fill(0);
    
    size_t totalProcessedSamples = 0;
    size_t bufferFlushCount = 0;
    
    while (srcBytesRemaining > 0) {
        const auto curr_consumed_bytes_counts_ = src_last_consumed_bytes_counts_[0];
        // 进行通道分离
        pcmReader_.process2(srcData + src_data_offset, srcBytesRemaining, 
                           ctx_->channel_samples, channel_buffer_max_capacitys_, channelWritePositions_, src_last_consumed_bytes_counts_);
        
        // 更新写入位置
        size_t newlyConsumedBytes = src_last_consumed_bytes_counts_[0] - curr_consumed_bytes_counts_;
        size_t samplesProcessed = newlyConsumedBytes / ctx_->format->frameSize();
        totalProcessedSamples += samplesProcessed;
        
        for (size_t i = 0; i < ctx_->format->channels(); ++i) {
            channelWritePositions_[i] += samplesProcessed;
        }

#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-通道分离] 处理了 " << samplesProcessed << " 样本, 通道写入位置: ";
        for (size_t i = 0; i < ctx_->format->channels(); ++i) {
            std::cout << "ch" << i << "=" << channelWritePositions_[i] << " ";
        }
        std::cout << std::endl;
#endif

        // 如果通道缓冲区已满，则触发下一阶段处理
        if (channelWritePositions_[0] == ctx_->channel_buffer_sample_count) {
            bufferFlushCount++;
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-通道分离] 缓冲区满，触发下一阶段处理 (第" << bufferFlushCount 
                      << "次), 时间戳=" << currentTimestamp << "s, 样本数=" 
                      << ctx_->channel_buffer_sample_count << std::endl;
                      
            // 输出每个通道的一些样本值
            for (size_t ch = 0; ch < ctx_->format->channels(); ++ch) {
                std::cout << "[DIAGNOSE-通道分离] 通道" << ch << "前5个样本: ";
                for (size_t s = 0; s < std::min<size_t>(5, ctx_->channel_buffer_sample_count); ++s) {
                    std::cout << ctx_->channel_samples[ch][s] << " ";
                }
                std::cout << std::endl;
            }
#endif
            
            emphasisPhase_->handleSamples(ctx_->channel_samples, ctx_->channel_buffer_sample_count, currentTimestamp);
            
            // 重置写入位置
            for (size_t i = 0; i < ctx_->format->channels(); ++i) {
                channelWritePositions_[i] = 0;
            }
        }

        
        // 更新源数据指针和剩余字节数
        src_data_offset += newlyConsumedBytes;
        srcBytesRemaining -= newlyConsumedBytes;
        
        // 更新时间戳
        double sampleRate = static_cast<double>(ctx_->format->sampleRate());
        currentTimestamp += static_cast<double>(samplesProcessed) / sampleRate;
    }

#ifdef ENABLED_DIAGNOSE
    double processedDuration = static_cast<double>(totalProcessedSamples) / ctx_->format->sampleRate();
    std::cout << "[DIAGNOSE-通道分离] 处理完成: 总样本数=" << totalProcessedSamples 
              << ", 持续时间=" << processedDuration << "s, 缓冲区刷新次数=" 
              << bufferFlushCount << std::endl;
#endif
}

} // namespace afp