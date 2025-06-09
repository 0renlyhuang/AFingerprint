#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "base/frame.h"
#include "base/ring_buffer.h"
#include "base/channel_array.h"
#include <unordered_set>
#include <vector>




namespace afp {

class HashComputationPhase {

public:
    HashComputationPhase(SignatureGenerationPipelineCtx* ctx);

    void handleFrame(ChannelArray<std::vector<Frame>>& channel_long_frames);

private:
    void consumeFrame(size_t channel);

    uint32_t computeTripleFrameHash(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);

    double computeTripleFrameCombinationScore(
        const Peak& anchorPeak,
        const Peak& targetPeak1,
        const Peak& targetPeak2);


    // 使用unordered_set作为查询结构以获得O(1)的查找性能
    struct PairHash {
        size_t operator()(const std::pair<uint32_t, double>& p) const {
            // 组合两个哈希值
            return std::hash<uint32_t>{}(p.first) ^ 
                    (std::hash<double>{}(p.second) << 1);
        }
    };

private:
    SignatureGenerationPipelineCtx* ctx_;
    const size_t symmetric_frame_range_;
    const SignatureGenerationConfig& signature_generation_config_;

    std::unordered_set<std::pair<uint32_t, double>, PairHash> existing_triple_frame_combinations_;
    std::vector<SignaturePoint> signature_points_;

    ChannelArray<RingBufferPtr<Frame>> frame_ring_buffers_;
};

} // namespace afp