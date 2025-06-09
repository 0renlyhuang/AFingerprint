#include "peak_extractor.h"

namespace afp {

PeakExtractor::PeakExtractor(SignatureGenerationPipelineCtx* ctx)
    : ctx_(ctx) {
}

std::vector<Peak> PeakExtractor::extractPeaks(
    const std::vector<FFTResult>& fft_results,
    int start_idx, int end_idx,
    float quantile_threshold) {
    
    std::vector<Peak> all_peaks;
    const auto& peak_config = ctx_->config->getPeakDetectionConfig();
    const size_t fft_size = ctx_->config->getFFTConfig().fftSize;
    
    // 计算分位数阈值
    float quantile_magnitude = calculateQuantileThreshold(
        fft_results, start_idx, end_idx, quantile_threshold);
    
    // 优化：一次遍历所有频率，避免重复检查
    for (int frame_idx = start_idx; frame_idx < end_idx; ++frame_idx) {
        const auto& current_frame = fft_results[frame_idx];
        
        for (size_t freq_idx = 0; freq_idx < fft_size / 2; ++freq_idx) {
            float current_freq = current_frame.frequencies[freq_idx];
            float current_magnitude = current_frame.magnitudes[freq_idx];
            
            // 检查是否在任何有效频段范围内
            if (current_freq < peak_config.minFreq || current_freq > peak_config.maxFreq) {
                continue;
            }
            
            // 检查双重幅度阈值
            if (current_magnitude <= quantile_magnitude || 
                current_magnitude < peak_config.minPeakMagnitude) {
                continue;
            }
            
            // 检查是否为时频域局部最大值
            if (!isLocalMaximum(fft_results, frame_idx, freq_idx, current_magnitude)) {
                continue;
            }
            
            // 满足所有条件，创建峰值
            Peak peak;
            peak.frequency = static_cast<uint32_t>(current_freq);
            peak.magnitude = current_magnitude;
            peak.timestamp = current_frame.timestamp;
            
            all_peaks.push_back(peak);

            if (ctx_->visualization_config->collectVisualizationData_) {
                ctx_->visualization_config->visualizationData_.allPeaks.emplace_back(peak.frequency, peak.timestamp, peak.magnitude);
            }
        }
    }
    
    return all_peaks;
}

bool PeakExtractor::isLocalMaximum(
    const std::vector<FFTResult>& fft_results,
    int frame_idx, size_t freq_idx,
    float current_magnitude) const {
    
    const auto& peak_config = ctx_->config->getPeakDetectionConfig();
    const size_t fft_size = ctx_->config->getFFTConfig().fftSize;
    
    // 检查频率维度上的局部最大值
    for (size_t j = 1; j <= peak_config.localMaxRange; ++j) {
        // 检查左边界
        if (freq_idx >= j) {
            if (current_magnitude <= fft_results[frame_idx].magnitudes[freq_idx - j]) {
                return false;
            }
        }
        // 检查右边界
        if (freq_idx + j < fft_size / 2) {
            if (current_magnitude <= fft_results[frame_idx].magnitudes[freq_idx + j]) {
                return false;
            }
        }
    }
    
    // 检查时间维度上的局部最大值
    for (size_t j = 1; j <= peak_config.timeMaxRange; ++j) {
        // 与前面的帧比较
        if (frame_idx >= static_cast<int>(j)) {
            if (current_magnitude <= fft_results[frame_idx - j].magnitudes[freq_idx]) {
                return false;
            }
        }
        
        // 与后面的帧比较
        if (frame_idx + static_cast<int>(j) < static_cast<int>(fft_results.size())) {
            if (current_magnitude <= fft_results[frame_idx + j].magnitudes[freq_idx]) {
                return false;
            }
        }
    }
    
    return true;
}

float PeakExtractor::calculateQuantileThreshold(
    const std::vector<FFTResult>& fft_results,
    int start_idx, int end_idx,
    float quantile) const {
    
    const auto& peak_config = ctx_->config->getPeakDetectionConfig();
    const size_t fft_size = ctx_->config->getFFTConfig().fftSize;
    
    std::vector<float> all_magnitudes;  // TODO: 性能优化，用同一个vector对象，避免重复创建
    
    // 收集窗口内所有帧的幅度值
    for (int frame_idx = start_idx; frame_idx < end_idx; ++frame_idx) {
        const auto& current_frame = fft_results[frame_idx];
        for (size_t freq_idx = peak_config.localMaxRange; 
             freq_idx < fft_size / 2 - peak_config.localMaxRange; 
             ++freq_idx) {
            
            float current_freq = current_frame.frequencies[freq_idx];
            
            // 只收集在有效频率范围内的幅度值
            if (current_freq >= peak_config.minFreq && 
                current_freq <= peak_config.maxFreq) {
                all_magnitudes.push_back(current_frame.magnitudes[freq_idx]);
            }
        }
    }
    
    // 计算分位数
    if (all_magnitudes.empty()) {
        return 0.0f;
    }
    
    std::sort(all_magnitudes.begin(), all_magnitudes.end());
    size_t n = all_magnitudes.size();
    
    float position = quantile * (n - 1);
    size_t lower_index = static_cast<size_t>(std::floor(position));
    size_t upper_index = static_cast<size_t>(std::ceil(position));
    
    if (lower_index == upper_index) {
        return all_magnitudes[lower_index];
    } else {
        float weight = position - lower_index;
        return all_magnitudes[lower_index] * (1.0f - weight) + 
               all_magnitudes[upper_index] * weight;
    }
}

} // namespace afp 