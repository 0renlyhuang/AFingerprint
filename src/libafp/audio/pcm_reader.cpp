#include "audio/pcm_reader.h"
#include <cstring>
#include <algorithm>

namespace afp {

PCMReader::PCMReader(const PCMFormat& format)
    : format_(format) {
}

void PCMReader::process(const void* data, size_t size, SampleCallback callback) {
    if (format_.layout() == ChannelLayout::Mono) {
        processMono(data, size, callback);
    } else {
        processStereo(data, size, callback);
    }
}

void PCMReader::process2(const uint8_t* src_data, size_t src_bytes_count, 
        ChannelArray<float*>& dst_buffers, 
        ChannelArray<size_t>& dst_max_capacitys, 
        ChannelArray<size_t>& dst_offsets,
        ChannelArray<size_t>& src_consumed_bytes_counts
        ){
    if (format_.layout() == ChannelLayout::Mono) {
        processMono2(src_data, src_bytes_count, dst_buffers[0], dst_max_capacitys[0], dst_offsets[0], src_consumed_bytes_counts[0]);
    } else {
        processStereo2(src_data, src_bytes_count, dst_buffers, dst_max_capacitys, dst_offsets, src_consumed_bytes_counts);
    }
}

void PCMReader::processMono2(const uint8_t* src_data, size_t src_bytes_count, 
        float* dst_buffer, 
        size_t dst_max_capacity, 
        size_t dst_offset,
        size_t& src_consumed_bytes_count
    ) {
    const uint8_t* ptr = src_data;
    size_t frameSize = format_.frameSize();  // 对于单声道，frameSize就是sampleSize
    
    // 计算源数据最多能提供多少个frame
    size_t maxSourceFrames = src_bytes_count / frameSize;
    
    // 计算目标缓冲区最多能容纳多少个frame
    size_t maxDestFrames = dst_max_capacity - dst_offset;
    
    // 实际处理的frame数量取两者的最小值
    size_t framesToProcess = std::min(maxSourceFrames, maxDestFrames);
    
    // 处理每个frame
    for (size_t i = 0; i < framesToProcess; ++i) {
        // 使用readSample读取并转换样本
        float sample = readSample(ptr);
        
        // 写入目标缓冲区，考虑偏移量
        dst_buffer[dst_offset + i] = sample;
        
        // 移动源数据指针
        ptr += frameSize;
    }
    
    // 更新消耗的源数据字节数
    src_consumed_bytes_count += framesToProcess * frameSize;
}

void PCMReader::processStereo2(const uint8_t* src_data, size_t src_bytes_count, 
        ChannelArray<float*>& dst_buffers, 
        ChannelArray<size_t>& dst_max_capacitys, 
        ChannelArray<size_t>& dst_offsets,
        ChannelArray<size_t>& src_consumed_bytes_counts) {
    
    const uint8_t* ptr = src_data;
    size_t frameSize = format_.frameSize();        // 立体声一帧包含两个样本
    size_t sampleSize = format_.sampleSize();      // 单个样本的字节数
    
    // 计算源数据最多能提供多少个frame
    size_t maxSourceFrames = src_bytes_count / frameSize;
    
    // 计算左右声道缓冲区分别能容纳多少个样本
    size_t maxLeftFrames = dst_max_capacitys[0] - dst_offsets[0];
    size_t maxRightFrames = dst_max_capacitys[1] - dst_offsets[1];
    
    // 实际处理的frame数量取三者的最小值
    size_t framesToProcess = std::min({maxSourceFrames, maxLeftFrames, maxRightFrames});
    
    // 处理每个frame（立体声frame包含左右两个样本）
    for (size_t i = 0; i < framesToProcess; ++i) {
        // 读取左声道样本
        float leftSample = readSample(ptr);
        dst_buffers[0][dst_offsets[0] + i] = leftSample;
        ptr += sampleSize;
        
        // 读取右声道样本
        float rightSample = readSample(ptr);
        dst_buffers[1][dst_offsets[1] + i] = rightSample;
        ptr += sampleSize;
    }
    
    // 更新消耗的源数据字节数（对于立体声，所有通道共享同一个源数据流）
    size_t totalConsumedBytes = framesToProcess * frameSize;
    for (size_t i = 0; i < src_consumed_bytes_counts.size(); ++i) {
        src_consumed_bytes_counts[i] += totalConsumedBytes;
    }
}

void PCMReader::processMono(const void* data, size_t size, SampleCallback callback) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t frameSize = format_.frameSize();
    size_t numFrames = size / frameSize;

    for (size_t i = 0; i < numFrames; ++i) {
        float sample = readSample(ptr);
        callback(sample, 0);
        ptr += frameSize;
    }
}

void PCMReader::processStereo(const void* data, size_t size, SampleCallback callback) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t frameSize = format_.frameSize();
    size_t numFrames = size / frameSize;

    for (size_t i = 0; i < numFrames; ++i) {
        float left = readSample(ptr);
        callback(left, 0);
        ptr += format_.sampleSize();
        
        float right = readSample(ptr);
        callback(right, 1);
        ptr += format_.sampleSize();
    }
}

float PCMReader::readSample(const uint8_t* ptr) {
    switch (format_.format()) {
        case SampleFormat::S8: {
            int8_t value = *reinterpret_cast<const int8_t*>(ptr);
            return static_cast<float>(value) / 128.0f;
        }
        case SampleFormat::U8: {
            uint8_t value = *ptr;
            return (static_cast<float>(value) - 128.0f) / 128.0f;
        }
        case SampleFormat::S16: {
            int16_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<int16_t>(ptr[0] | (ptr[1] << 8));
            } else {
                value = static_cast<int16_t>((ptr[0] << 8) | ptr[1]);
            }
            return static_cast<float>(value) / 32768.0f;
        }
        case SampleFormat::U16: {
            uint16_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<uint16_t>(ptr[0] | (ptr[1] << 8));
            } else {
                value = static_cast<uint16_t>((ptr[0] << 8) | ptr[1]);
            }
            return (static_cast<float>(value) - 32768.0f) / 32768.0f;
        }
        case SampleFormat::S24: {
            int32_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<int32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
            } else {
                value = static_cast<int32_t>((ptr[0] << 16) | (ptr[1] << 8) | ptr[2]);
            }
            // 符号扩展
            if (value & 0x800000) {
                value |= 0xFF000000;
            }
            return static_cast<float>(value) / 8388608.0f;
        }
        case SampleFormat::U24: {
            uint32_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<uint32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16));
            } else {
                value = static_cast<uint32_t>((ptr[0] << 16) | (ptr[1] << 8) | ptr[2]);
            }
            return (static_cast<float>(value) - 8388608.0f) / 8388608.0f;
        }
        case SampleFormat::S32: {
            int32_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<int32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
            } else {
                value = static_cast<int32_t>((ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3]);
            }
            return static_cast<float>(value) / 2147483648.0f;
        }
        case SampleFormat::U32: {
            uint32_t value;
            if (format_.endianness() == Endianness::Little) {
                value = static_cast<uint32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
            } else {
                value = static_cast<uint32_t>((ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3]);
            }
            return (static_cast<float>(value) - 2147483648.0f) / 2147483648.0f;
        }
        case SampleFormat::F32: {
            float value;
            if (format_.endianness() == Endianness::Little) {
                uint32_t bits = static_cast<uint32_t>(ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24));
                std::memcpy(&value, &bits, sizeof(float));
            } else {
                uint32_t bits = static_cast<uint32_t>((ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3]);
                std::memcpy(&value, &bits, sizeof(float));
            }
            return value;
        }
        case SampleFormat::F64: {
            double value;
            if (format_.endianness() == Endianness::Little) {
                uint64_t bits = static_cast<uint64_t>(ptr[0]) |
                              (static_cast<uint64_t>(ptr[1]) << 8) |
                              (static_cast<uint64_t>(ptr[2]) << 16) |
                              (static_cast<uint64_t>(ptr[3]) << 24) |
                              (static_cast<uint64_t>(ptr[4]) << 32) |
                              (static_cast<uint64_t>(ptr[5]) << 40) |
                              (static_cast<uint64_t>(ptr[6]) << 48) |
                              (static_cast<uint64_t>(ptr[7]) << 56);
                std::memcpy(&value, &bits, sizeof(double));
            } else {
                uint64_t bits = (static_cast<uint64_t>(ptr[0]) << 56) |
                              (static_cast<uint64_t>(ptr[1]) << 48) |
                              (static_cast<uint64_t>(ptr[2]) << 40) |
                              (static_cast<uint64_t>(ptr[3]) << 32) |
                              (static_cast<uint64_t>(ptr[4]) << 24) |
                              (static_cast<uint64_t>(ptr[5]) << 16) |
                              (static_cast<uint64_t>(ptr[6]) << 8) |
                              static_cast<uint64_t>(ptr[7]);
                std::memcpy(&value, &bits, sizeof(double));
            }
            return static_cast<float>(value);
        }
        default:
            return 0.0f;
    }
}

} // namespace afp 