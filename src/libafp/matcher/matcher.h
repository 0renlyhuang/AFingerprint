#pragma once
#include <functional>
#include <memory>
#include "signature/signature_generator.h"
#include "afp/icatalog.h"
#include "afp/media_item.h"
#include "signature/signature_matcher.h"
#include "config/performance_config.h"
#include "afp/pcm_format.h"
#include "afp/imatcher.h"

namespace afp {

class Matcher : public IMatcher {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;

    Matcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config, const PCMFormat& format);
    ~Matcher() override;

    // 添加音频数据
    bool appendStreamBuffer(const void* buffer, 
                          size_t bufferSize,
                          double startTimestamp) override;

    // 设置匹配回调
    void setMatchCallback(MatchCallback callback) override {
        matchCallback_ = callback;
        signatureMatcher_->setMatchNotifyCallback(callback);
    }

    std::unique_ptr<SignatureMatcher> signatureMatcher_;

private:
    std::shared_ptr<ICatalog> catalog_;
    PCMFormat format_;  // 存储音频格式信息，包括通道数
    std::unique_ptr<SignatureGenerator> generator_;
    
    MatchCallback matchCallback_;
};

} // namespace afp 