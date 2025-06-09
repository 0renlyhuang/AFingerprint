#pragma once

namespace afp {
    // 定义峰值结构，用于跨帧存储
struct Peak {
    uint32_t frequency;   // 频率 (Hz)
    float magnitude;      // 幅度
    double timestamp;     // 时间戳 (秒)
};

}