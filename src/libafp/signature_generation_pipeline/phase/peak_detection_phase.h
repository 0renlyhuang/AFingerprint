#pragma once

#include "signature_generation_pipeline/signature_generation_pipeline_ctx.h"
#include "signature_generation_pipeline/peak_detection/frequency_band_manager.h"
#include "signature_generation_pipeline/peak_detection/peak_extractor.h"
#include "signature_generation_pipeline/phase/long_frame_building_phase.h"
#include "base/channel_array.h"
#include "base/fft_result.h"
#include "base/peek.h"
#include "base/ring_buffer.h"

namespace afp {

// 峰值检测状态跟踪结构
struct PeakDetectionState {
    double current_window_start_time = 0.0;    // 当前检测窗口开始时间
    double current_window_end_time = 0.0;      // 当前检测窗口结束时间  
    double first_beyond_window_timestamp = 0.0; // 第一个超过当前窗口的元素时间戳
    size_t elements_beyond_window = 0;         // 超过当前窗口的元素数量
    bool window_initialized = false;          // 窗口是否已初始化
    
    void reset() {
        current_window_start_time = 0.0;
        current_window_end_time = 0.0;
        elements_beyond_window = 0;
        window_initialized = false;
    }
};

class PeakDetectionPhase {

public:
    PeakDetectionPhase(SignatureGenerationPipelineCtx* ctx);

    ~PeakDetectionPhase();

    void attach(LongFrameBuildingPhase* longFrameBuildingPhase);


    void handleShortFrames(ChannelArray<std::vector<FFTResult>>& fft_results);

private:
    void extractPeaks(std::vector<FFTResult>& fft_results, size_t channel_i);
    
    // 在指定窗口内检测峰值
    void detectPeaksInWindow(
        const std::vector<FFTResult>& fft_results,
        int start_idx, int end_idx,
        size_t channel_i);
    
    // 计算动态峰值配额
    int calculateDynamicPeakQuota(
        const std::vector<FFTResult>& fft_results,
        int start_idx, int end_idx,
        size_t channel_i);
    
    // 按频段分配峰值配额
    std::vector<int> allocatePeakQuotas(
        const std::vector<Peak>& peaks,
        int total_quota);
    
    // 过滤峰值到指定配额
    std::vector<Peak> filterPeaksToQuota(
        const std::vector<Peak>& peaks,
        const std::vector<int>& band_quotas);

private:
    SignatureGenerationPipelineCtx* ctx_;
    const PeakDetectionConfig& peak_config_;
    LongFrameBuildingPhase* longFrameBuildingPhase_;
    
    double peek_detection_duration_;

    // 每个通道的FFT结果缓存（使用ring buffer）
    ChannelArray<RingBufferPtr<FFTResult>> fft_results_cache_;
    
    // 检测到的峰值结果
    ChannelArray<std::vector<Peak>> detected_peaks_;
    
    // 峰值检测状态跟踪
    ChannelArray<PeakDetectionState> detection_states_;
    
    // 工具类
    std::unique_ptr<FrequencyBandManager> band_manager_;
    std::unique_ptr<PeakExtractor> peak_extractor_;
};

} // namespace afp