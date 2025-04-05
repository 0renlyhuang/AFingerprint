#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include "media_item.h"
#include "signature/signature_generator.h"
#include "catalog.h"

namespace afp {

// 前向声明
struct MatchResult;

// 表示单个候选匹配结果
struct MatchCandidate {
    const MediaItem* mediaItem;         // 匹配的媒体项
    double offset;                      // 时间偏移（秒）
    double confidence;                  // 匹配置信度
    size_t matchCount;                  // 匹配点数量
    double lastMatchTime;               // 最后一次匹配的时间戳
    std::map<uint32_t, double> matchedHashes;  // 已匹配的哈希值及其对应的时间戳
    size_t id;                          // 唯一标识符
};

class SignatureMatcher {
public:
    using MatchNotifyCallback = std::function<void(const MatchResult&)>;
    
    // 构造函数 - 接收目录参数
    SignatureMatcher(const Catalog& catalog);
    
    // 析构函数
    ~SignatureMatcher();
    
    // 设置匹配成功通知回调
    void setMatchNotifyCallback(MatchNotifyCallback callback) {
        matchNotifyCallback_ = callback;
    }
    
    // 处理来自流式输入的指纹点并执行匹配
    void processQuerySignature(const std::vector<SignaturePoint>& querySignature);
    
    // 获取当前候选结果集
    const std::vector<MatchCandidate>& candidates() const {
        return candidates_;
    }
    
    // 清空所有候选结果
    void clearCandidates() {
        candidates_.clear();
        mediaItemCandidates_.clear();
    }
    
private:
    // 执行哈希匹配
    void performMatching(const std::vector<SignaturePoint>& querySignature);
    
    // 处理单个哈希匹配
    void processHashMatch(uint32_t hash, double queryTime, 
                         const MediaItem& mediaItem, double targetTime);
    
    // 更新候选结果状态（淘汰过期的，评估匹配置信度）
    void updateCandidates(double currentTimestamp);
    
    // 根据置信度评估候选结果是否达到通知标准
    bool evaluateCandidate(MatchCandidate& candidate, double currentTimestamp);
    
    // 淘汰过期的候选结果
    void removeExpiredCandidates(double currentTimestamp);
    
    // 限制候选结果数量
    void limitCandidatesCount();
    
private:
    const Catalog& catalog_;  // 存储目录引用
    std::vector<MatchCandidate> candidates_;  // 所有候选结果
    std::unordered_map<const MediaItem*, std::vector<size_t>> mediaItemCandidates_;  // 媒体项到候选索引的映射
    MatchNotifyCallback matchNotifyCallback_;  // 匹配通知回调
    static size_t nextCandidateId_;     // 下一个候选ID
    
    // 哈希匹配优化结构
    struct TargetSignatureInfo {
        const MediaItem* mediaItem;
        std::unordered_map<uint32_t, std::vector<double>> hashTimestamps;  // 哈希值到时间戳的映射
    };
    std::vector<TargetSignatureInfo> targetSignaturesInfo_;  // 预处理的目标签名信息
    
    // 配置参数
    static constexpr size_t kMaxCandidates = 50;           // 最大候选结果数
    static constexpr double kMatchExpireTime = 5.0;        // 候选结果过期时间（秒）
    static constexpr double kMinConfidenceThreshold = 0.4; // 最小置信度阈值
    static constexpr size_t kMinMatchesRequired = 15;      // 最小匹配点数
};

} // namespace afp 