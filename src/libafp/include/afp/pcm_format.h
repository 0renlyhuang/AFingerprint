#pragma once

#include <cstdint>
#include <string>

namespace afp {

// 定义音频格式枚举
enum class SampleFormat {
    S8,     // 8位有符号整数
    U8,     // 8位无符号整数
    S16,    // 16位有符号整数
    U16,    // 16位无符号整数
    S24,    // 24位有符号整数
    U24,    // 24位无符号整数
    S32,    // 32位有符号整数
    U32,    // 32位无符号整数
    F32,    // 32位浮点数
    F64     // 64位浮点数
};

// 定义字节序
enum class Endianness {
    Little,
    Big
};

// 定义通道布局
enum class ChannelLayout {
    Mono,
    Stereo,
    Surround,
    Custom
};

class PCMFormat {
public:
    PCMFormat() = default;
    
    // 构造函数
    PCMFormat(uint32_t sampleRate, 
             SampleFormat format,
             uint32_t channels,
             Endianness endianness = Endianness::Little,
             ChannelLayout layout = ChannelLayout::Stereo,
             bool interleaved = true)
        : sampleRate_(sampleRate)
        , format_(format)
        , channels_(channels)
        , endianness_(endianness)
        , layout_(layout)
        , interleaved_(interleaved) {
    }

    // Getters
    uint32_t sampleRate() const { return sampleRate_; }
    SampleFormat format() const { return format_; }
    uint32_t channels() const { return channels_; }
    Endianness endianness() const { return endianness_; }
    ChannelLayout layout() const { return layout_; }
    bool interleaved() const { return interleaved_; }

    // 获取样本大小（字节）
    uint32_t sampleSize() const {
        switch (format_) {
            case SampleFormat::S8:
            case SampleFormat::U8:
                return 1;
            case SampleFormat::S16:
            case SampleFormat::U16:
                return 2;
            case SampleFormat::S24:
            case SampleFormat::U24:
                return 3;
            case SampleFormat::S32:
            case SampleFormat::U32:
            case SampleFormat::F32:
                return 4;
            case SampleFormat::F64:
                return 8;
            default:
                return 0;
        }
    }

    // 获取帧大小（字节）
    uint32_t frameSize() const {
        return sampleSize() * channels_;
    }

    // 获取格式描述字符串
    std::string toString() const {
        std::string result = "Sample Rate: " + std::to_string(sampleRate_) + " Hz, ";
        result += "Format: " + formatToString() + ", ";
        result += "Channels: " + std::to_string(channels_) + ", ";
        result += "Endianness: ";
        result += (endianness_ == Endianness::Little ? "Little" : "Big");
        result += ", ";
        result += "Layout: " + layoutToString() + ", ";
        result += "Interleaved: " + std::string(interleaved_ ? "Yes" : "No");
        return result;
    }

private:
    std::string formatToString() const {
        switch (format_) {
            case SampleFormat::S8: return "S8";
            case SampleFormat::U8: return "U8";
            case SampleFormat::S16: return "S16";
            case SampleFormat::U16: return "U16";
            case SampleFormat::S24: return "S24";
            case SampleFormat::U24: return "U24";
            case SampleFormat::S32: return "S32";
            case SampleFormat::U32: return "U32";
            case SampleFormat::F32: return "F32";
            case SampleFormat::F64: return "F64";
            default: return "Unknown";
        }
    }

    std::string layoutToString() const {
        switch (layout_) {
            case ChannelLayout::Mono: return "Mono";
            case ChannelLayout::Stereo: return "Stereo";
            case ChannelLayout::Surround: return "Surround";
            case ChannelLayout::Custom: return "Custom";
            default: return "Unknown";
        }
    }

    uint32_t sampleRate_ = 0;
    SampleFormat format_ = SampleFormat::S16;
    uint32_t channels_ = 2;
    Endianness endianness_ = Endianness::Little;
    ChannelLayout layout_ = ChannelLayout::Stereo;
    bool interleaved_ = true;
};

} // namespace afp 