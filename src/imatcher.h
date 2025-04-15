#pragma once
#include <functional>
#include <memory>
#include "signature/signature_generator.h"
#include "icatalog.h"
#include "media_item.h"
#include "signature/signature_matcher.h"
#include "config/performance_config.h"
#include "audio/pcm_format.h"

namespace afp {

struct MatchResult {
    const MediaItem& mediaItem;
    double offset;           // 时间偏移（秒）
    double confidence;       // 匹配置信度
    std::vector<SignaturePoint> matchedPoints;  // 匹配的点
    size_t id;               // 唯一标识符
};

class IMatcher {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;

    virtual ~IMatcher() = default;

    // 添加音频数据
    virtual bool appendStreamBuffer(const void* buffer, 
                                  size_t bufferSize,
                                  double startTimestamp) = 0;

    // 设置匹配回调
    virtual void setMatchCallback(MatchCallback callback) = 0;
};

} // namespace afp 