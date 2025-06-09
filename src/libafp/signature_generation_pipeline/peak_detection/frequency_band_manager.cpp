#include "frequency_band_manager.h"

namespace afp {

FrequencyBandManager::FrequencyBandManager(float min_freq, float max_freq, size_t num_bands) {
    generateLogFrequencyBands(min_freq, max_freq, num_bands);
}

void FrequencyBandManager::generateLogFrequencyBands(float min_freq, float max_freq, size_t num_bands) {
    bands_.clear();
    
    if (num_bands == 0 || min_freq >= max_freq) {
        return;
    }
    
    // 使用对数尺度计算频段边界
    float logMinFreq = std::log10(min_freq);
    float logMaxFreq = std::log10(max_freq);
    float logStep = (logMaxFreq - logMinFreq) / num_bands;
    
    for (size_t i = 0; i < num_bands; ++i) {
        float logStart = logMinFreq + i * logStep;
        float logEnd = logMinFreq + (i + 1) * logStep;
        
        float freqStart = std::pow(10.0f, logStart);
        float freqEnd = std::pow(10.0f, logEnd);
        
        bands_.emplace_back(freqStart, freqEnd);
    }
}

int FrequencyBandManager::findBandIndex(float frequency) const {
    for (size_t i = 0; i < bands_.size(); ++i) {
        if (frequency >= bands_[i].min_freq && frequency < bands_[i].max_freq) {
            return static_cast<int>(i);
        }
    }
    return -1;  // 未找到对应频段
}

std::vector<float> FrequencyBandManager::getBandWeights() const {
    std::vector<float> weights;
    weights.reserve(bands_.size());
    for (const auto& band : bands_) {
        weights.push_back(band.weight);
    }
    return weights;
}

float FrequencyBandManager::getTotalWeight() const {
    float total = 0.0f;
    for (const auto& band : bands_) {
        total += band.weight;
    }
    return total;
}

} // namespace afp 