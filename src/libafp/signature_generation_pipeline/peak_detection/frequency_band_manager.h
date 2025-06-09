#pragma once

#include <vector>
#include <utility>
#include <cmath>

namespace afp {

struct FrequencyBand {
    float min_freq;
    float max_freq;
    float center_freq;
    float weight;  // 优先级权重
    
    FrequencyBand(float min, float max) 
        : min_freq(min), max_freq(max), center_freq((min + max) / 2.0f) {
        calculateWeight();
    }
    
private:
    void calculateWeight() {
        // 定义重点频率范围：150-2500Hz (中频)
        const float priorityMinFreq = 150.0f;
        const float priorityMaxFreq = 2500.0f;
        
        if (center_freq >= priorityMinFreq && center_freq <= priorityMaxFreq) {
            weight = 3.0f;  // 中频段：最高优先级
        } else if (center_freq > priorityMaxFreq) {
            weight = 2.0f;  // 高频段：中等优先级
        } else {
            weight = 1.0f;  // 低频段：最低优先级
        }
    }
};

class FrequencyBandManager {
public:
    FrequencyBandManager(float min_freq, float max_freq, size_t num_bands);
    
    // 获取所有频段
    const std::vector<FrequencyBand>& getBands() const { return bands_; }
    
    // 根据频率找到对应的频段索引
    int findBandIndex(float frequency) const;
    
    // 获取频段权重
    std::vector<float> getBandWeights() const;
    
    // 计算总权重
    float getTotalWeight() const;

private:
    std::vector<FrequencyBand> bands_;
    
    // 生成基于对数尺度的频段
    void generateLogFrequencyBands(float min_freq, float max_freq, size_t num_bands);
};

} // namespace afp 