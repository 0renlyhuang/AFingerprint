#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "audio/pcm_reader.h"
#include "signature_generation_pipeline/phase/emphasis_phase.h"
#include "base/channel_array.h"

namespace afp {

class ChannelSplitPhase {

public:
    ChannelSplitPhase(SignatureGenerationPipelineCtx* ctx);

    ~ChannelSplitPhase();

    void attach(EmphasisPhase* emphasisPhase);

    void handleAudioData(const void* buffer, size_t bufferSize, double startTimestamp);

private:
    PCMReader pcmReader_;
    SignatureGenerationPipelineCtx* ctx_;
    EmphasisPhase* emphasisPhase_;
    
    // 跟踪每个通道缓冲区的当前写入位置
    ChannelArray<size_t> channelWritePositions_;
    // 每个通道缓冲区的最大容量
    ChannelArray<size_t> channel_buffer_max_capacitys_;

    // 记录每个通道上次处理时消耗的字节数
    ChannelArray<size_t> src_last_consumed_bytes_counts_;
    
    // 检查是否有通道缓冲区已满，如果满了则触发下一阶段处理
    void checkAndProcessFullBuffers(double timestamp);
};

} // namespace afp