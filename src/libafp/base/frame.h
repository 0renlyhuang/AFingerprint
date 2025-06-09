#pragma once

#include <vector>
#include "base/peek.h"

namespace afp {

    // 帧结构，存储一个时间帧的峰值点
struct Frame {
    std::vector<Peak> peaks;
    double timestamp;     // 帧时间戳 (秒)
};

}