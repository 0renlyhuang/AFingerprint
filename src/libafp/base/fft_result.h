#pragma once

#include <vector>

namespace afp { 
    // FFT结果结构，存储短帧的FFT结果
struct FFTResult {
    std::vector<float> magnitudes;
    std::vector<float> frequencies;
    double timestamp;     // 短帧时间戳 (秒)
};

}