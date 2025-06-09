#include "long_frame_building_phase.h"
#include <iostream>

namespace afp {

LongFrameBuildingPhase::LongFrameBuildingPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx)
    {
        const auto& peak_config = ctx_->config->getPeakDetectionConfig();
        const auto& signature_config = ctx_->config->getSignatureGenerationConfig();
        max_peak_count_ = peak_config.maxPeaksPerFrameLimit * std::ceil(peak_config.peakTimeDuration / signature_config.frameDuration);

        peak_buffers_.fill(std::vector<Peak>());
        wnd_infos_.fill(WndInfo());
        for (size_t i = 0; i < ctx_->channel_count; i++) {
            peak_buffers_[i].reserve(max_peak_count_);

            wnd_infos_[i].start_time = 0.0;
            wnd_infos_[i].end_time = signature_config.frameDuration;
        }

#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-长帧构建] LongFrameBuildingPhase 初始化:" << std::endl;
        std::cout << "  通道数: " << ctx_->channel_count << std::endl;
        std::cout << "  长帧持续时间: " << signature_config.frameDuration << "s" << std::endl;
        std::cout << "  峰值检测持续时间: " << peak_config.peakTimeDuration << "s" << std::endl;
        std::cout << "  每长帧最大峰值数: " << max_peak_count_ << std::endl;
        std::cout << "  每帧最大峰值限制: " << peak_config.maxPeaksPerFrameLimit << std::endl;
        
        for (size_t i = 0; i < ctx_->channel_count; i++) {
            std::cout << "  通道" << i << "初始窗口: [" << wnd_infos_[i].start_time 
                      << "s, " << wnd_infos_[i].end_time << "s]" << std::endl;
        }
#endif
}

LongFrameBuildingPhase::~LongFrameBuildingPhase() {
}

void LongFrameBuildingPhase::attach(HashComputationPhase* hash_computation_phase) {
    hash_computation_phase_ = hash_computation_phase;
}

void LongFrameBuildingPhase::handlePeaks(ChannelArray<std::vector<Peak>>& peaks) {
#ifdef ENABLED_DIAGNOSE
    size_t total_peaks = 0;
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        total_peaks += peaks[i].size();
    }
    std::cout << "[DIAGNOSE-长帧构建] 开始处理峰值: 总峰值数=" << total_peaks << std::endl;
    
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        std::cout << "  通道" << i << ": " << peaks[i].size() << "个新峰值, 缓冲区已有" 
                  << peak_buffers_[i].size() << "个峰值" << std::endl;
        if (!peaks[i].empty()) {
            const auto& first_peak = peaks[i].front();
            const auto& last_peak = peaks[i].back();
            std::cout << "    时间范围: [" << first_peak.timestamp << "s, " 
                      << last_peak.timestamp << "s]" << std::endl;
        }
    }
#endif

    for (size_t i = 0; i < ctx_->channel_count; i++) {
        handleChannelPeaks(i, peaks[i]);
    }

#ifdef ENABLED_DIAGNOSE
    size_t total_long_frames = 0;
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        total_long_frames += long_frames_[i].size();
    }
    std::cout << "[DIAGNOSE-长帧构建] 生成长帧统计: 总长帧数=" << total_long_frames << std::endl;
    
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        std::cout << "  通道" << i << ": " << long_frames_[i].size() << "个长帧" << std::endl;
        for (size_t j = 0; j < long_frames_[i].size(); j++) {
            const auto& frame = long_frames_[i][j];
            std::cout << "    长帧" << j << ": 时间戳=" << frame.timestamp 
                      << "s, 峰值数=" << frame.peaks.size() << std::endl;
        }
    }
#endif

    hash_computation_phase_->handleFrame(long_frames_);
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        long_frames_[i].clear();
    }
}

void LongFrameBuildingPhase::handleChannelPeaks(size_t channel, std::vector<Peak>& peaks) {
    auto& wnd_info = wnd_infos_[channel];
    auto& peak_buffer = peak_buffers_[channel];

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 处理通道" << channel << "峰值:" << std::endl;
    std::cout << "  当前窗口: [" << wnd_info.start_time << "s, " << wnd_info.end_time << "s]" << std::endl;
    std::cout << "  新增峰值数: " << peaks.size() << "详情:[";
    for (const auto& peak : peaks) {
        std::cout << peak.frequency << "Hz@" << peak.timestamp << "s(" << peak.magnitude << ") ";
    }
    std::cout << "]" << std::endl;
    std::cout << "  缓冲区已有峰值数: " << peak_buffer.size() << std::endl;

    size_t processed_peaks = 0;
    size_t window_slides = 0;
#endif


    
    for (const auto& peak : peaks) {
        
#ifdef ENABLED_DIAGNOSE
        processed_peaks++;
        std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "处理峰值" << processed_peaks 
                  << "/" << peaks.size() << ": 时间=" << peak.timestamp << "s, 频率=" 
                  << peak.frequency << "Hz" << std::endl;
#endif

        // 处理峰值超出当前窗口的情况
        while (peak.timestamp >= wnd_info.end_time) {
            window_slides++;
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "峰值超出窗口(第" 
                      << window_slides << "次滑动):" << std::endl;
            std::cout << "  峰值时间: " << peak.timestamp << "s >= 窗口结束: " << wnd_info.end_time << "s" << std::endl;
            std::cout << "  当前缓冲区峰值数: " << peak_buffer.size() << std::endl;
#endif
            
            // 如果当前窗口有峰值，先消费掉
            if (!peak_buffer.empty()) {
                // 验证缓冲区中的峰值是否在当前窗口范围内
                bool hasValidPeaks = peak_buffer[0].timestamp >= wnd_info.start_time && 
                                    peak_buffer[0].timestamp < wnd_info.end_time;
                
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "窗口有峰值，验证有效性:" << std::endl;
                std::cout << "  第一个峰值时间: " << peak_buffer[0].timestamp << "s" << std::endl;
                std::cout << "  窗口范围: [" << wnd_info.start_time << "s, " << wnd_info.end_time << "s)" << std::endl;
                std::cout << "  有效峰值: " << (hasValidPeaks ? "是" : "否") << std::endl;
#endif
                
                if (hasValidPeaks) {
                    consumePeaks(channel);
                }
            } else {
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "空窗口，跳过消费: " 
                          << wnd_info.start_time << "s - " << wnd_info.end_time << "s" << std::endl;
#endif
            }

            // 滑动窗口到下一个位置
            double old_start = wnd_info.start_time;
            double old_end = wnd_info.end_time;
            wnd_info.start_time = wnd_info.end_time;
            wnd_info.end_time = wnd_info.start_time + ctx_->config->getSignatureGenerationConfig().frameDuration;
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "滑动窗口: [" 
                      << old_start << "s, " << old_end << "s) -> [" 
                      << wnd_info.start_time << "s, " << wnd_info.end_time << "s)" << std::endl;
#endif
        }

        peak_buffer.push_back(peak);
        
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "添加峰值到缓冲区: " 
                  << peak.timestamp << "s (" << peak.frequency << "Hz), 缓冲区新大小: " 
                  << peak_buffer.size() << std::endl;
#endif
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "峰值处理完成:" << std::endl;
    std::cout << "  处理峰值数: " << processed_peaks << std::endl;
    std::cout << "  窗口滑动次数: " << window_slides << std::endl;
    std::cout << "  最终缓冲区大小: " << peak_buffer.size() << std::endl;
    std::cout << "  最终窗口: [" << wnd_info.start_time << "s, " << wnd_info.end_time << "s]" << std::endl;
#endif
}

void LongFrameBuildingPhase::consumePeaks(size_t channel) {
    auto& peak_buffer = peak_buffers_[channel];

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "开始消费峰值:" << std::endl;
    std::cout << "  窗口时间戳: " << wnd_infos_[channel].start_time << "s" << std::endl;
    std::cout << "  峰值数量: " << peak_buffer.size() << std::endl;
    
    if (!peak_buffer.empty()) {
        const auto& first_peak = peak_buffer.front();
        const auto& last_peak = peak_buffer.back();
        std::cout << "  峰值时间范围: [" << first_peak.timestamp << "s, " 
                  << last_peak.timestamp << "s]" << std::endl;
        
        // 输出峰值频率分布
        std::map<int, int> freq_dist;
        for (const auto& peak : peak_buffer) {
            int freq_band = static_cast<int>(peak.frequency / 500) * 500; // 500Hz为单位
            freq_dist[freq_band]++;
        }
        std::cout << "  频率分布: ";
        for (const auto& fd : freq_dist) {
            std::cout << fd.first << "Hz(" << fd.second << ") ";
        }
        std::cout << std::endl;
    }
#endif

    Frame frame;
    frame.peaks = std::move(peak_buffer);
    frame.timestamp = wnd_infos_[channel].start_time;
    
    peak_buffer.clear();

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 通道" << channel << "生成长帧: 时间戳=" 
              << frame.timestamp << "s, 峰值数=" << frame.peaks.size() << std::endl;
    std::cout << "  峰值详情:[";
    for (const auto& peak : frame.peaks) {
        std::cout << peak.frequency << "Hz@" << peak.timestamp << "s(" << peak.magnitude << ") ";
    }
    std::cout << "]" << std::endl;
#endif

    long_frames_[channel].push_back(frame);
}

void LongFrameBuildingPhase::flushPeaks() {
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 开始清空所有剩余峰值缓冲区" << std::endl;
#endif
    
    size_t total_flushed = 0;
    for (size_t i = 0; i < ctx_->channel_count; i++) {
        if (!peak_buffers_[i].empty()) {
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-长帧构建] 通道" << i << "清空剩余峰值: " 
                      << peak_buffers_[i].size() << "个" << std::endl;
#endif
            total_flushed += peak_buffers_[i].size();
            consumePeaks(i);
        }
    }
    
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-长帧构建] 清空完成，总计清空" << total_flushed << "个峰值" << std::endl;
#endif
}
}