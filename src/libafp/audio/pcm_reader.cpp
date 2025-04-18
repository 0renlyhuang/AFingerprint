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