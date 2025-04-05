#pragma once
#include <functional>
#include <memory>
#include "signature/signature_generator.h"
#include "catalog.h"
#include "media_item.h"
#include "signature/signature_matcher.h"
#include "config/performance_config.h"

namespace afp {

struct MatchResult {
    const MediaItem& mediaItem;
    double offset;           // 时间偏移（秒）
    double confidence;       // 匹配置信度
    std::vector<SignaturePoint> matchedPoints;  // 匹配的点
    size_t id;               // 唯一标识符
};

class Matcher {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;

    Matcher(const Catalog& catalog, std::shared_ptr<PerformanceConfig> config, size_t sampleRate);
    ~Matcher();

    // 添加音频数据
    bool appendStreamBuffer(const float* buffer, 
                          size_t bufferSize,
                          double startTimestamp);

    // 设置匹配回调
    void setMatchCallback(MatchCallback callback) {
        matchCallback_ = callback;
        signatureMatcher_->setMatchNotifyCallback(callback);
    }

private:
    const Catalog& catalog_;
    std::unique_ptr<SignatureGenerator> generator_;
    std::unique_ptr<SignatureMatcher> signatureMatcher_;
    MatchCallback matchCallback_;
};

} // namespace afp 