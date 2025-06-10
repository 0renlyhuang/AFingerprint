#pragma once
#include <vector>
#include "afp/pcm_format.h"

namespace afp {

struct SignaturePoint {
    uint32_t hash;           // 音频指纹hash值
    double timestamp;        // 时间戳（秒）
    uint32_t frequency;      // 频率（Hz）
    uint32_t amplitude;      // 振幅
};

class ISignatureGenerator {
public:
    virtual ~ISignatureGenerator() = default;

    // 初始化生成器
    virtual bool init(const PCMFormat& format) = 0;

    // 添加音频数据
    virtual bool appendStreamBuffer(const void* buffer, 
                                  size_t bufferSize,
                                  double startTimestamp) = 0;

    // 刷新内部音频数据缓冲区
    virtual void flush() = 0;

    // 获取生成的指纹
    virtual std::vector<SignaturePoint> signature() const = 0;
    
    // 重置所有已生成的签名
    virtual void resetSignatures() = 0;
};

} // namespace afp 
