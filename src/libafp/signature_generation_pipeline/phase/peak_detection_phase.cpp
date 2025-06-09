#include "peak_detection_phase.h"
#include <iostream>
#include <algorithm>
#include <map>

namespace afp {

PeakDetectionPhase::PeakDetectionPhase(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx)
    , peek_detection_duration_(ctx->config->getPeakDetectionConfig().peakTimeDuration)
    , peak_config_(ctx->config->getPeakDetectionConfig())
    , fft_results_cache_()
    , detected_peaks_()
    , detection_states_()
{
    // 初始化频段管理器
    band_manager_ = std::make_unique<FrequencyBandManager>(
        static_cast<float>(peak_config_.minFreq),
        static_cast<float>(peak_config_.maxFreq),
        peak_config_.numFrequencyBands);
    
    // 初始化峰值提取器
    peak_extractor_ = std::make_unique<PeakExtractor>(ctx);
    
    // 计算每个通道缓存的容量
    const auto shortFrameDuration = static_cast<double>(ctx_->config->getFFTConfig().hopSize) / ctx_->sample_rate;
    const auto peakDetectionFrameCount = std::ceil(peek_detection_duration_ / shortFrameDuration);
    const auto totalBufferSize = peakDetectionFrameCount + 2 * peak_config_.timeMaxRange;

    // 初始化每个通道的ring buffer和数据结构
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        fft_results_cache_[channel_i] = std::make_unique<RingBuffer<FFTResult>>(totalBufferSize);
        detected_peaks_[channel_i].clear();
        detection_states_[channel_i].reset();
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-峰值检测] PeakDetectionPhase 初始化:" << std::endl;
    std::cout << "  峰值检测持续时间: " << peek_detection_duration_ << "s" << std::endl;
    std::cout << "  短帧持续时间: " << shortFrameDuration << "s" << std::endl;
    std::cout << "  峰值检测区域帧数: " << peakDetectionFrameCount << std::endl;
    std::cout << "  边界保护帧数: " << peak_config_.timeMaxRange << std::endl;
    std::cout << "  缓存总容量: " << totalBufferSize << std::endl;
    std::cout << "  频段配置: " << peak_config_.minFreq << "-" << peak_config_.maxFreq 
              << "Hz, " << peak_config_.numFrequencyBands << "个频段" << std::endl;
    std::cout << "  峰值数量限制: " << peak_config_.minPeaksPerFrame << "-" 
              << peak_config_.maxPeaksPerFrameLimit << std::endl;
#endif
}

PeakDetectionPhase::~PeakDetectionPhase() = default;

void PeakDetectionPhase::attach(LongFrameBuildingPhase* longFrameBuildingPhase) {
    longFrameBuildingPhase_ = longFrameBuildingPhase;
}

void PeakDetectionPhase::handleShortFrames(ChannelArray<std::vector<FFTResult>>& fft_results) {
#ifdef ENABLED_DIAGNOSE
    bool is_satisfied_to_detect = false;
    size_t total_fft_results = 0;
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        total_fft_results += fft_results[channel_i].size();
    }
    std::cout << "[DIAGNOSE-峰值检测] 开始处理短帧: 总FFT结果数=" << total_fft_results << std::endl;
#endif

    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        auto& fftr = fft_results[channel_i];
        size_t fftr_remaining = fftr.size();
        size_t fftr_offset = 0;
        auto* fftr_ring_buffer = fft_results_cache_[channel_i].get();
        auto& detection_state = detection_states_[channel_i];
        
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-峰值检测] 处理通道" << channel_i << ": 新增FFT结果=" 
                  << fftr.size() << ", 缓存状态=" << fftr_ring_buffer->size() 
                  << "/" << fftr_ring_buffer->capacity() << "缓存详情:[";
        for (size_t i = 0; i < fftr_ring_buffer->size(); ++i) {
            std::cout << (*fftr_ring_buffer)[i].timestamp << "s ";
        }
        std::cout << "]";
        if (detection_state.window_initialized) {
            std::cout << "  当前窗口: [" << detection_state.current_window_start_time 
                      << "s-" << detection_state.current_window_end_time << "s]" << std::endl;
            std::cout << "  超出窗口元素数: " << detection_state.elements_beyond_window << std::endl;
        } else {
            std::cout << "  窗口未初始化" << std::endl;
        }
#endif
        
        if (!detection_state.window_initialized && fftr.size() > 0) {
            detection_state.current_window_start_time = fftr[0].timestamp;
            detection_state.current_window_end_time = detection_state.current_window_start_time + peek_detection_duration_;
            detection_state.window_initialized = true;
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-窗口初始化] 通道" << channel_i << "初始化检测窗口:" << std::endl;
            std::cout << "  起始时间戳: " << fftr[0].timestamp << "s" << std::endl;
            std::cout << "  窗口时间: [" << detection_state.current_window_start_time << "s-" 
                      << detection_state.current_window_end_time << "s]" << std::endl;
            std::cout << "  窗口持续时间: " << peek_detection_duration_ << "s" << std::endl;
#endif
        }

        for (auto& fftr_item : fftr) {
            // 当ring buffer 中只有peak_config_.timeMaxRange个元素时，把时间窗滑动到当前这个fftr_item所属的窗口
            if (fftr_ring_buffer->size() == peak_config_.timeMaxRange) {
                auto next_window_start_time = detection_state.current_window_end_time;
                while (fftr_item.timestamp >= next_window_start_time) {
                    detection_state.current_window_start_time = next_window_start_time;
                    detection_state.current_window_end_time = next_window_start_time + peek_detection_duration_;
                    next_window_start_time += peek_detection_duration_;
                    
#ifdef ENABLED_DIAGNOSE
                    std::cout << "[DIAGNOSE-峰值检测窗口调整] 通道" << channel_i << "时间戳" << fftr_item.timestamp 
                              << "s超出当前窗口，调整窗口到: [" << detection_state.current_window_start_time 
                              << "s-" << detection_state.current_window_end_time << "s]" << std::endl;
#endif
                }
            }

            // 写入数据到ring buffer
            fftr_ring_buffer->push_back(fftr_item);
            
#ifdef ENABLED_DIAGNOSE
            if (fftr_ring_buffer->size() % 10 == 0) { // 每10个元素输出一次状态
                std::cout << "[DIAGNOSE-峰值检测数据写入, 每写入10个元素输出一次状态] 通道" << channel_i << "写入时间戳" << fftr_item.timestamp 
                          << "s，缓存大小=" << fftr_ring_buffer->size() << std::endl;
            }
#endif

            // 如果不够安全距离，继续累加元素
            if (fftr_ring_buffer->size() <= peak_config_.timeMaxRange) {
#ifdef ENABLED_DIAGNOSE
                if (fftr_ring_buffer->size() == peak_config_.timeMaxRange) {
                    std::cout << "[DIAGNOSE-安全距离] 通道" << channel_i << "达到前缘安全距离: " 
                              << peak_config_.timeMaxRange << "个元素" << std::endl;
                }
#endif
                continue;
            }

            // 如果当前元素时间戳小于等于当前窗口结束时间戳，继续累加元素
            if (fftr_item.timestamp <= detection_state.current_window_end_time) {
                continue;
            }
            
            ++detection_state.elements_beyond_window;
            if (detection_state.elements_beyond_window == 1) {
                detection_state.first_beyond_window_timestamp = fftr_item.timestamp;
                
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-峰值检测超出窗口] 通道" << channel_i << "第一个超出窗口的元素:" << std::endl;
                std::cout << "  时间戳: " << fftr_item.timestamp << "s" << std::endl;
                std::cout << "  窗口结束时间: " << detection_state.current_window_end_time << "s" << std::endl;
#endif
            }
            
            // 如果不超过尾部安全距离，继续累加元素
            if (detection_state.elements_beyond_window < peak_config_.timeMaxRange) {
#ifdef ENABLED_DIAGNOSE
                if (detection_state.elements_beyond_window % 5 == 0) { // 每5个元素输出一次
                    std::cout << "[DIAGNOSE-峰值检测累积超出] 通道" << channel_i << "超出窗口元素数: " 
                              << detection_state.elements_beyond_window << "/" << peak_config_.timeMaxRange << std::endl;
                }
#endif
                continue;
            }

#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-峰值检测检测条件满足] 通道" << channel_i << "满足峰值检测条件:" << std::endl;
            std::cout << "  缓存大小: " << fftr_ring_buffer->size() << std::endl;
            std::cout << "  检测窗口: [" << detection_state.current_window_start_time 
                      << "s-" << detection_state.current_window_end_time << "s]" << std::endl;
            std::cout << "  超出窗口元素数: " << detection_state.elements_beyond_window 
                      << " (需要: " << peak_config_.timeMaxRange << ")" << std::endl;
#endif

            // 超过当前时间窗口结束时间戳的元素已经有 peak_config_.timeMaxRange 个，进行峰值检测
            // 除去前后安全距离，至少要有一个元素
            if (fftr_ring_buffer->size() >= 2 * peak_config_.timeMaxRange + 1) {
                std::vector<FFTResult> current_results = fftr_ring_buffer->getRange(0, fftr_ring_buffer->size());
                
#ifdef ENABLED_DIAGNOSE
                const int start_idx = peak_config_.timeMaxRange;
                const int end_idx = current_results.size() - peak_config_.timeMaxRange;
                std::cout << "[DIAGNOSE-峰值检测开始检测] 通道" << channel_i << "开始峰值检测:" << std::endl;
                std::cout << "  检测区域: [" << start_idx << ", " << end_idx << ") / [0, " << current_results.size() << ")" << std::endl;
                std::cout << "  检测区域大小: " << (end_idx - start_idx) << "个短帧" << std::endl;
                if (end_idx > start_idx) {
                    std::cout << "  时间范围: ["; 
                    for (int i = start_idx; i < end_idx; ++i) {
                        std::cout << current_results[i].timestamp << "s ";
                    }
                    std::cout << "]" << std::endl;
                }
                is_satisfied_to_detect = true;
#endif
                
                detectPeaksInWindow(current_results, peak_config_.timeMaxRange, current_results.size() - peak_config_.timeMaxRange, channel_i);
            }

            // 移动窗口（移除到剩下2 * peak_config.timeMaxRange个样本）
            size_t keep_count = 2 * peak_config_.timeMaxRange;
            if (fftr_ring_buffer->size() < keep_count) {
                std::throw_with_nested(std::runtime_error("fftr_ring_buffer->size() < keep_count"));
            }
            
            size_t remove_count = fftr_ring_buffer->size() - keep_count;
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-峰值检测窗口移动] 通道" << channel_i << "移动窗口:" << std::endl;
            std::cout << "  移除元素数: " << remove_count << std::endl;
            std::cout << "  保留元素数: " << keep_count << std::endl;
            std::cout << "  移动前缓存大小: " << fftr_ring_buffer->size() << std::endl;
#endif
            
            fftr_ring_buffer->moveWindow(remove_count);
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "  移动后缓存大小: " << fftr_ring_buffer->size() << std::endl;
#endif

            // 更新窗口到ring buffer 中倒数第peak_config_.timeMaxRange个元素所属的窗口
            const auto next_wnd_start_idx = fftr_ring_buffer->size() - peak_config_.timeMaxRange;
            auto& next_wnd_start_fftr_item = (*fftr_ring_buffer)[next_wnd_start_idx];
            
            // 将窗口滑动到next_wnd_start_fftr_item所属的窗口区间
            auto next_window_start_time = detection_state.current_window_end_time;
            auto old_window_start = detection_state.current_window_start_time;
            auto old_window_end = detection_state.current_window_end_time;
            
            while (next_wnd_start_fftr_item.timestamp >= next_window_start_time) {
                detection_state.current_window_start_time = next_window_start_time;
                detection_state.current_window_end_time = next_window_start_time + peek_detection_duration_;
                next_window_start_time = detection_state.current_window_end_time;
            }
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-峰值检测窗口更新] 通道" << channel_i << "更新检测窗口:" << std::endl;
            std::cout << "  参考元素时间戳: " << next_wnd_start_fftr_item.timestamp << "s (索引:" << next_wnd_start_idx << ")" << std::endl;
            std::cout << "  旧窗口: [" << old_window_start << "s-" << old_window_end << "s]" << std::endl;
            std::cout << "  新窗口: [" << detection_state.current_window_start_time << "s-" 
                      << detection_state.current_window_end_time << "s]" << std::endl;
#endif
            
            detection_state.elements_beyond_window = 0;
            detection_state.first_beyond_window_timestamp = 0.0;
            
            // 更新first_beyond_window_timestamp和elements_beyond_window
            for (size_t i = 1; i < peak_config_.timeMaxRange; ++i) {
                if ((*fftr_ring_buffer)[next_wnd_start_idx + i].timestamp > detection_state.current_window_end_time) {
                    ++detection_state.elements_beyond_window;
                    if (detection_state.elements_beyond_window == 1) {
                        detection_state.first_beyond_window_timestamp = (*fftr_ring_buffer)[next_wnd_start_idx + i].timestamp;
                    }
                }
            }
            
#ifdef ENABLED_DIAGNOSE
            std::cout << "  重新计算超出窗口元素数: " << detection_state.elements_beyond_window << std::endl;
            if (detection_state.elements_beyond_window > 0) {
                std::cout << "  首个超出元素时间戳: " << detection_state.first_beyond_window_timestamp << "s" << std::endl;
            }
#endif
        }
    }

    // 如果检测到峰值，则通知长帧构建阶段处理峰值
    bool has_peaks = false;
    size_t total_peaks = 0;
    for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
        total_peaks += detected_peaks_[channel_i].size();
        if (detected_peaks_[channel_i].size() > 0) {
            has_peaks = true;
        }
    }

#ifdef ENABLED_DIAGNOSE
    if (is_satisfied_to_detect) {
        std::cout << "[DIAGNOSE-峰值检测] 峰值检测完成: 总峰值数=" << total_peaks 
                << ", 有峰值=" << (has_peaks ? "是" : "否") << std::endl;
        for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
            std::cout << "  通道" << channel_i << ": " << detected_peaks_[channel_i].size() << "个峰值" << std::endl;
        }
    } else {
        std::cout << "[DIAGNOSE-峰值检测] 峰值检测还未满足要求";
        for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
            std::cout << "  通道" << channel_i << ": 当前缓存-[";

            auto* fftr_ring_buffer = fft_results_cache_[channel_i].get();
            for (size_t i = 0; i < fftr_ring_buffer->size(); ++i) {
                std::cout << (*fftr_ring_buffer)[i].timestamp << "s ";
            }
            std::cout << "] 个"<< fftr_ring_buffer->size() << "元素, 当前时间窗-[" << detection_states_[channel_i].current_window_start_time << "s-" 
                      << detection_states_[channel_i].current_window_end_time << "s]" << std::endl;
        }
    }

#endif

    if (has_peaks) {
        longFrameBuildingPhase_->handlePeaks(detected_peaks_);

        for (size_t channel_i = 0; channel_i < ctx_->channel_count; ++channel_i) {
            detected_peaks_[channel_i].clear();
        }
    }
}

void PeakDetectionPhase::detectPeaksInWindow(
    const std::vector<FFTResult>& fft_results,
    int start_idx, int end_idx,
    size_t channel_i) {

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-峰值检测] 通道" << channel_i << "开始窗口内峰值检测:" << std::endl;
    std::cout << "  FFT结果数量: " << fft_results.size() << std::endl;
    std::cout << "  检测范围: [" << start_idx << ", " << end_idx << ")" << std::endl;
    std::cout << "  分位数阈值: " << peak_config_.quantileThreshold << std::endl;
#endif
    
    // 使用峰值提取器检测峰值
    std::vector<Peak> raw_peaks = peak_extractor_->extractPeaks(
        fft_results, start_idx, end_idx, peak_config_.quantileThreshold);
    
    if (raw_peaks.empty()) {
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-峰值检测] 通道" << channel_i << "未检测到原始峰值" << std::endl;
#endif
        return;
    }
    
    // 计算动态峰值配额
    int dynamic_quota = calculateDynamicPeakQuota(fft_results, start_idx, end_idx, channel_i);
    
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-峰值检测] 通道" << channel_i << "原始峰值统计:" << std::endl;
    std::cout << "  检测到 " << raw_peaks.size() << " 个原始峰值" << std::endl;
    std::cout << "  动态配额: " << dynamic_quota << std::endl;
    
    // // 输出峰值频率分布
    // std::map<int, int> freq_dist;
    // for (const auto& peak : raw_peaks) {
    //     int freq_band = static_cast<int>(peak.frequency / 500) * 500; // 500Hz为单位
    //     freq_dist[freq_band]++;
    // }
    // std::cout << "  频率分布: ";
    // for (const auto& fd : freq_dist) {
    //     std::cout << fd.first << "Hz(" << fd.second << ") ";
    // }
    // std::cout << std::endl;
#endif
    
    // 如果峰值数量超过配额，进行过滤
    std::vector<Peak> final_peaks;
    if (static_cast<int>(raw_peaks.size()) > dynamic_quota) {
        std::vector<int> band_quotas = allocatePeakQuotas(raw_peaks, dynamic_quota);
        final_peaks = filterPeaksToQuota(raw_peaks, band_quotas);
        
#ifdef ENABLED_DIAGNOSE
        // std::cout << "[DIAGNOSE-峰值过滤] 通道" << channel_i << "需要过滤峰值" << std::endl;
        // std::cout << "  频段配额分配: ";
        // for (size_t i = 0; i < band_quotas.size(); ++i) {
        //     if (band_quotas[i] > 0) {
        //         const auto& bands = band_manager_->getBands();
        //         std::cout << bands[i].min_freq << "-" << bands[i].max_freq 
        //                   << "Hz(" << band_quotas[i] << ") ";
        //     }
        // }
        // std::cout << std::endl;
#endif
    } else {
        final_peaks = std::move(raw_peaks);
        
#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-峰值检测] 通道" << channel_i << "峰值数量未超配额，无需过滤" << std::endl;
#endif
    }
    
    // 存储检测结果
    detected_peaks_[channel_i].insert(
        detected_peaks_[channel_i].end(), final_peaks.begin(), final_peaks.end());
    
#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-峰值检测] 通道" << channel_i << "最终保留 " << final_peaks.size() 
              << " 个峰值，累计峰值数: " << detected_peaks_[channel_i].size() << std::endl;
              
    // 输出保留的峰值详情（前5个）
    std::cout << "  保留的峰值(前5个): ";
    for (size_t i = 0; i < std::min<size_t>(5, final_peaks.size()); ++i) {
        const auto& peak = final_peaks[i];
        std::cout << peak.frequency << "Hz@" << peak.timestamp << "s(" 
                  << peak.magnitude << ") ";
    }
    std::cout << std::endl;
#endif
}

int PeakDetectionPhase::calculateDynamicPeakQuota(
    const std::vector<FFTResult>& fft_results,
    int start_idx, int end_idx,
    size_t channel_i) {
    
    const size_t fft_size = ctx_->fft_size;
    
    // 计算频段能量
    std::vector<float> band_energies(band_manager_->getBands().size(), 0.0f);
    std::vector<float> band_noise_levels(band_manager_->getBands().size(), 0.0f);
    
    // 收集每个频段的能量和噪声水平
    for (int frame_idx = start_idx; frame_idx < end_idx; ++frame_idx) {
        const auto& frame = fft_results[frame_idx];
        
        for (size_t freq_idx = 0; freq_idx < fft_size / 2; ++freq_idx) {
            float freq = frame.frequencies[freq_idx];
            float magnitude = frame.magnitudes[freq_idx];
            
            int band_idx = band_manager_->findBandIndex(freq);
            if (band_idx >= 0) {
                band_energies[band_idx] += magnitude * magnitude;
            }
        }
    }
    
    // 归一化能量
    int frame_count = end_idx - start_idx;
    if (frame_count > 0) {
        for (float& energy : band_energies) {
            energy /= frame_count;
        }
    }
    
    // 计算总能量和平均信噪比（简化版本）
    float total_energy = 0.0f;
    for (float energy : band_energies) {
        total_energy += energy;
    }
    
    // 基于能量动态计算峰值数量
    float energy_factor = std::min(1.0f, total_energy / 1000.0f);  // 假设1000为参考能量
    float snr_factor = 0.5f;  // 简化的信噪比因子
    
    float combined_factor = peak_config_.energyWeightFactor * energy_factor + 
                           peak_config_.snrWeightFactor * snr_factor;
    
    int dynamic_count = static_cast<int>(
        peak_config_.minPeaksPerFrame + 
        combined_factor * (peak_config_.maxPeaksPerFrameLimit - peak_config_.minPeaksPerFrame));
    
    int final_quota = std::max(static_cast<int>(peak_config_.minPeaksPerFrame), 
                              std::min(dynamic_count, static_cast<int>(peak_config_.maxPeaksPerFrameLimit)));

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-动态配额] 通道" << channel_i << "配额计算:" << std::endl;
    std::cout << "  总能量: " << total_energy << ", 能量因子: " << energy_factor << std::endl;
    std::cout << "  SNR因子: " << snr_factor << ", 组合因子: " << combined_factor << std::endl;
    std::cout << "  计算配额: " << dynamic_count << ", 最终配额: " << final_quota << std::endl;
    std::cout << "  配额范围: [" << peak_config_.minPeaksPerFrame << ", " 
              << peak_config_.maxPeaksPerFrameLimit << "]" << std::endl;
#endif

    return final_quota;
}

std::vector<int> PeakDetectionPhase::allocatePeakQuotas(
    const std::vector<Peak>& peaks,
    int total_quota) {
    
    const auto& bands = band_manager_->getBands();
    std::vector<int> band_quotas(bands.size(), 0);
    
    // 将峰值按频段分组
    std::vector<std::vector<Peak>> band_peaks(bands.size());
    for (const auto& peak : peaks) {
        int band_idx = band_manager_->findBandIndex(static_cast<float>(peak.frequency));
        if (band_idx >= 0) {
            band_peaks[band_idx].push_back(peak);
        }
    }
    
    // 根据权重分配初始配额
    std::vector<float> band_weights = band_manager_->getBandWeights();
    float total_weight = band_manager_->getTotalWeight();
    
    int allocated_quota = 0;
    for (size_t i = 0; i < bands.size(); ++i) {
        band_quotas[i] = static_cast<int>((band_weights[i] / total_weight) * total_quota);
        allocated_quota += band_quotas[i];
    }
    
    // 分配剩余配额（由于整数除法可能产生的余数）
    int remaining_quota = total_quota - allocated_quota;
    
    // 按权重降序分配剩余配额
    std::vector<std::pair<float, size_t>> weighted_bands;
    for (size_t i = 0; i < bands.size(); ++i) {
        weighted_bands.emplace_back(band_weights[i], i);
    }
    std::sort(weighted_bands.begin(), weighted_bands.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    for (const auto& wb : weighted_bands) {
        if (remaining_quota <= 0) break;
        band_quotas[wb.second]++;
        remaining_quota--;
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-初始配额] 初始配额分配完成:" << std::endl;
    for (size_t i = 0; i < bands.size(); ++i) {
        std::cout << "  频段" << i << ": 权重=" << band_weights[i] 
                  << ", 初始配额=" << band_quotas[i] 
                  << ", 峰值数=" << band_peaks[i].size() << std::endl;
    }
#endif
    
    // 优化分配策略：处理空频段和峰值不足的频段，将多余配额收回
    std::vector<int> insufficient_bands;   // 峰值数量少于配额的频段（包括空频段）
    std::vector<int> need_more_bands;      // 峰值数量超过配额的频段
    
    for (size_t i = 0; i < band_peaks.size(); ++i) {
        int peak_count = static_cast<int>(band_peaks[i].size());
        if (peak_count < band_quotas[i]) {
            insufficient_bands.push_back(i);
        } else if (peak_count > band_quotas[i]) {
            need_more_bands.push_back(i);
        }
    }
    
    // 从峰值不足的频段收回多余配额
    remaining_quota = 0;
    for (int band : insufficient_bands) {
        int actual_peaks = static_cast<int>(band_peaks[band].size());
        int excess_quota = band_quotas[band] - actual_peaks;
        remaining_quota += excess_quota;
        band_quotas[band] = actual_peaks;  // 设置为实际峰值数量
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-配额回收] 从峰值不足的频段回收配额:" << std::endl;
    std::cout << "  峰值不足频段数: " << insufficient_bands.size() << std::endl;
    std::cout << "  需要更多配额频段数: " << need_more_bands.size() << std::endl;
    std::cout << "  回收的配额数: " << remaining_quota << std::endl;
#endif
    
    // 按优先级将剩余配额分配给需要更多峰值的频段
    // 优先分配给高权重频段
    std::sort(need_more_bands.begin(), need_more_bands.end(),
              [&](int a, int b) { return band_weights[a] > band_weights[b]; });
    
    while (remaining_quota > 0 && !need_more_bands.empty()) {
        bool allocated = false;
        for (int band : need_more_bands) {
            if (remaining_quota <= 0) break;
            if (band_quotas[band] < static_cast<int>(band_peaks[band].size())) {
                band_quotas[band]++;
                remaining_quota--;
                allocated = true;
                
#ifdef ENABLED_DIAGNOSE
                std::cout << "[DIAGNOSE-配额重分配] 分配1个配额给频段" << band 
                          << ", 新配额=" << band_quotas[band] 
                          << ", 剩余配额=" << remaining_quota << std::endl;
#endif
            }
        }
        
        // 如果这一轮没有分配任何配额，说明所有频段都已满足，退出循环
        if (!allocated) {
#ifdef ENABLED_DIAGNOSE
            std::cout << "[DIAGNOSE-配额重分配] 所有频段都已满足，停止分配" << std::endl;
#endif
            break;
        }
        
        // 移除已满足的频段
        need_more_bands.erase(
            std::remove_if(need_more_bands.begin(), need_more_bands.end(),
                [&](int band) { return band_quotas[band] >= static_cast<int>(band_peaks[band].size()); }),
            need_more_bands.end()
        );
    }

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-配额分配] 频段配额详情:" << std::endl;
    for (size_t i = 0; i < bands.size(); ++i) {
        std::cout << "  频段" << i << " [" << bands[i].min_freq << "-" << bands[i].max_freq 
                  << "Hz]: 峰值数=" << band_peaks[i].size() << ", 配额=" << band_quotas[i] 
                  << ", 权重=" << band_weights[i] << std::endl;
    }

    size_t final_allocated_quota = 0;
    for (size_t i = 0; i < band_quotas.size(); ++i) {
        final_allocated_quota += band_quotas[i];
    }

    std::cout << "  总配额: " << total_quota << ", 最终分配: " << final_allocated_quota 
              << ", 剩余: " << (total_quota - final_allocated_quota) << std::endl;
#endif
    
    return band_quotas;
}

std::vector<Peak> PeakDetectionPhase::filterPeaksToQuota(
    const std::vector<Peak>& peaks,
    const std::vector<int>& band_quotas) {
    
    const auto& bands = band_manager_->getBands();
    std::vector<std::vector<Peak>> band_peaks(bands.size());
    
    // 将峰值按频段分组
    for (const auto& peak : peaks) {
        int band_idx = band_manager_->findBandIndex(static_cast<float>(peak.frequency));
        if (band_idx >= 0) {
            band_peaks[band_idx].push_back(peak);
        }
    }
    
    // 在每个频段内按幅度排序并选择顶部峰值
    std::vector<Peak> filtered_peaks;
    for (size_t i = 0; i < band_peaks.size(); ++i) {
        if (band_quotas[i] == 0 || band_peaks[i].empty()) {
            continue;
        }
        
        // 按幅度降序排序
        std::sort(band_peaks[i].begin(), band_peaks[i].end(),
                  [](const Peak& a, const Peak& b) { return a.magnitude > b.magnitude; });
        
        // 选择顶部峰值
        int peaks_to_select = std::min(band_quotas[i], static_cast<int>(band_peaks[i].size()));

#ifdef ENABLED_DIAGNOSE
        std::cout << "[DIAGNOSE-峰值过滤] 频段" << i << " [" << bands[i].min_freq 
                  << "-" << bands[i].max_freq << "Hz]: 选择" << peaks_to_select 
                  << "/" << band_peaks[i].size() << "个峰值, 详情:[";
        for (size_t j = 0; j < band_peaks[i].size(); ++j) {
            const auto& peak = band_peaks[i][j];
            std::cout << (j < peaks_to_select ? "✅" : "❌") << peak.frequency << "Hz@" << peak.timestamp << "s(" 
                      << peak.magnitude << ") ";
        }
        std::cout << "]" << std::endl;
#endif

        for (int j = 0; j < peaks_to_select; ++j) {
            filtered_peaks.push_back(band_peaks[i][j]);
        }
    }
    
    // 按时间戳排序
    std::sort(filtered_peaks.begin(), filtered_peaks.end(),
              [](const Peak& a, const Peak& b) { return a.timestamp < b.timestamp; });

#ifdef ENABLED_DIAGNOSE
    std::cout << "[DIAGNOSE-峰值过滤] 过滤完成: 原始" << peaks.size() 
              << "个峰值 -> 保留" << filtered_peaks.size() << "个峰值" << std::endl;
#endif
    
    return filtered_peaks;
}

} // namespace afp