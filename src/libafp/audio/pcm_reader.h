#pragma once

#include "afp/pcm_format.h"
#include <cstdint>
#include <vector>
#include <functional>
#include "base/channel_array.h"

namespace afp {

class PCMReader {
public:
    // 回调函数类型，用于处理读取到的样本
    using SampleCallback = std::function<void(float sample, uint32_t channel)>;

    // 构造函数
    explicit PCMReader(const PCMFormat& format);

    // 处理PCM数据
    void process(const void* data, size_t size, SampleCallback callback);

    void process2(const uint8_t* src_data, size_t src_bytes_count, 
        ChannelArray<float*>& dst_buffers, 
        ChannelArray<size_t>& dst_max_capacitys, 
        ChannelArray<size_t>& dst_offsets,
        ChannelArray<size_t>& src_consumed_bytes_counts
    );

private:
    // 从原始数据读取样本值
    float readSample(const uint8_t* ptr);
    
    // 处理单声道数据
    void processMono(const void* data, size_t size, SampleCallback callback);

    void processMono2(const uint8_t* src_data, size_t src_bytes_count, 
        float* dst_buffer, 
        size_t dst_max_capacity, 
        size_t dst_offset,
        size_t& src_consumed_bytes_count);
    
    void processStereo2(const uint8_t* src_data, size_t src_bytes_count, 
        ChannelArray<float*>& dst_buffers, 
        ChannelArray<size_t>& dst_max_capacitys, 
        ChannelArray<size_t>& dst_offsets,
        ChannelArray<size_t>& src_consumed_bytes_counts);
    
    // 处理立体声数据
    void processStereo(const void* data, size_t size, SampleCallback callback);

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