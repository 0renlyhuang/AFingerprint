#pragma once

#include "audio/pcm_format.h"
#include <cstdint>
#include <vector>
#include <functional>

namespace afp {

class PCMReader {
public:
    // 回调函数类型，用于处理读取到的样本
    using SampleCallback = std::function<void(float sample, uint32_t channel)>;

    // 构造函数
    explicit PCMReader(const PCMFormat& format);

    // 处理PCM数据
    void process(const void* data, size_t size, const SampleCallback& callback);

private:
    // 从原始数据读取样本值
    template<typename T>
    float readSample(const void* data, size_t offset, bool isSigned) const;

    // 处理交错格式的数据
    void processInterleaved(const void* data, size_t size, const SampleCallback& callback);

    // 处理非交错格式的数据
    void processNonInterleaved(const void* data, size_t size, const SampleCallback& callback);

    // 根据格式获取最大值
    float getMaxValue() const;

    // 交换字节序
    template<typename T>
    T swapEndian(T value) const;

    PCMFormat format_;
    float maxValue_;
};

} // namespace afp 