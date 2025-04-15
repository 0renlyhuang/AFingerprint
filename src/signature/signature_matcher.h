#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <memory>
#include <string>
#include "media_item.h"
#include "signature/signature_generator.h"
#include "catalog.h"
#include "config/performance_config.h"

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
    size_t channelCount;                // 音频通道数
    size_t totalTargetHashesCount;      // 目标音频的总特征数
};

class SignatureMatcher {
public:
    using MatchNotifyCallback = std::function<void(const MatchResult&)>;
    
    // 构造函数 - 接收目录参数
    SignatureMatcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config);
    
    // 析构函数
    ~SignatureMatcher();
    
    // 设置匹配成功通知回调
    void setMatchNotifyCallback(MatchNotifyCallback callback) {
        matchNotifyCallback_ = callback;
    }
    
    // 处理来自流式输入的指纹点并执行匹配
    void processQuerySignature(const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount);
    
    // 获取当前候选结果集
    const std::vector<MatchCandidate>& candidates() const {
        return candidates_;
    }
    
    // 清空所有候选结果
    void clearCandidates() {
        candidates_.clear();
        mediaItemCandidates_.clear();
    }

    // 添加候选音频
    void addCandidate(const std::string& id, const std::vector<SignaturePoint>& signatures);

    // 移除候选音频
    void removeCandidate(const std::string& id);

    // 处理新的音频指纹
    void processSignatures(const std::vector<SignaturePoint>& signatures);

    // 获取匹配结果
    std::vector<MatchResult> getMatches() const;

    // 清除所有匹配结果
    void clearMatches();

private:
    // 执行哈希匹配
    void performMatching(const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount);
    
    // 处理单个哈希匹配
    void processHashMatch(uint32_t hash, double queryTime, 
                         const MediaItem& mediaItem, double targetTime, size_t inputChannelCount, size_t totalTargetHashesCount);
    
    // 更新候选结果状态（淘汰过期的，评估匹配置信度）
    void updateCandidates(double currentTimestamp);
    
    // 根据置信度评估候选结果是否达到通知标准
    bool evaluateCandidate(MatchCandidate& candidate, double currentTimestamp);
    
    // 淘汰过期的候选结果
    void removeExpiredCandidates(double currentTimestamp);
    
    // 限制候选结果数量
    void limitCandidatesCount();
    
private:
    std::shared_ptr<ICatalog> catalog_;  // 存储目录引用
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
    
    std::shared_ptr<IPerformanceConfig> config_;
    size_t maxCandidates_;         // 最大候选结果数
    double matchExpireTime_;       // 匹配过期时间 (秒)
    float minConfidenceThreshold_; // 最小置信度阈值
    size_t minMatchesRequired_;    // 最小匹配点数要求
    double offsetTolerance_;       // 时间偏移容忍度 (秒)
};

} // namespace afp 