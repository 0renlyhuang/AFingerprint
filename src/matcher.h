#pragma once
#include <functional>
#include <memory>
#include "signature_generator.h"
#include "catalog.h"
#include "media_item.h"

namespace afp {

struct MatchResult {
    const MediaItem& mediaItem;
    double offset;           // 时间偏移（秒）
    double confidence;       // 匹配置信度
    std::vector<SignaturePoint> matchedPoints;  // 匹配的点
};

class Matcher {
public:
    using MatchCallback = std::function<void(const MatchResult&)>;

    Matcher(const Catalog& catalog);
    ~Matcher();

    // 添加音频数据
    bool appendStreamBuffer(const float* buffer, 
                          size_t bufferSize,
                          double startTimestamp);

    // 设置匹配回调
    void setMatchCallback(MatchCallback callback) {
        matchCallback_ = callback;
    }

private:
    // 执行匹配
    void performMatching();

    // 计算两个指纹序列的相似度
    double computeSimilarity(const std::vector<SignaturePoint>& query,
                           const std::vector<SignaturePoint>& target,
                           double& offset);

private:
    const Catalog& catalog_;
    std::unique_ptr<SignatureGenerator> generator_;
    MatchCallback matchCallback_;
    
    // 匹配参数
    static constexpr size_t kMinMatches = 3;
    static constexpr double kMinConfidence = 0.5;
};

} // namespace afp 