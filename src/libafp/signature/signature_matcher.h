#pragma once
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <string>
#include "afp/media_item.h"
#include "signature/signature_generator.h"
#include "catalog/catalog.h"
#include "config/performance_config.h"
#include "debugger/visualization.h"

namespace afp {

struct CandidateSessionKey {
    int32_t offset; // ms
    const std::vector<SignaturePoint>* signature;

    bool operator==(const CandidateSessionKey& other) const {
        return offset == other.offset && signature == other.signature;
    }
};

} // namespace afp


namespace std {
    template <>
    struct hash<afp::CandidateSessionKey> {
        size_t operator()(const afp::CandidateSessionKey& k) const {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(k.signature);
#if INTPTR_MAX == INT32_MAX  // 32-bit platform
            uint32_t ptr_low16 = static_cast<uint32_t>(ptr & 0xFFFF);
            uint32_t offset_low16 = static_cast<uint32_t>(k.offset & 0xFFFF);
            return static_cast<size_t>((ptr_low16 << 16) | offset_low16);
#else  // 64-bit platform
            uint64_t ptr_low32 = static_cast<uint64_t>(ptr & 0xFFFFFFFF);
            uint64_t offset_low32 = static_cast<uint64_t>(k.offset & 0xFFFFFFFF);
            return static_cast<size_t>((ptr_low32 << 32) | offset_low32);
#endif
        }
    };
}

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
    
    // Visualization methods
    // Enable/disable visualization data collection
    void enableVisualization(bool enable) {
        collectVisualizationData_ = enable;
    }
    
    // Get visualization data
    VisualizationData getVisualizationData() const {
        return visualizationData_;
    }
    
    // Set title for visualization
    void setVisualizationTitle(const std::string& title) {
        visualizationData_.title = title;
    }
    
    // Set audio file path for visualization
    void setAudioFilePath(const std::string& path) {
        visualizationData_.audioFilePath = path;
    }
    
    // Save visualization data to file (JSON only)
    bool saveVisualization(const std::string& filename) const;
    
    // Save top matching sessions data to JSON file
    bool saveSessionsData(const std::string& filename) const;
    
    // Save comparison data for source and query fingerprints
    bool saveComparisonData(const VisualizationData& sourceData, 
                           const std::string& sourceFilename,
                           const std::string& queryFilename,
                           const std::string& sessionsFilename) const;

private:
    std::shared_ptr<ICatalog> catalog_;  // 存储目录引用
    std::vector<MatchCandidate> candidates_;  // 所有候选结果
    std::unordered_map<const MediaItem*, std::vector<size_t>> mediaItemCandidates_;  // 媒体项到候选索引的映射
    MatchNotifyCallback matchNotifyCallback_;  // 匹配通知回调
    static size_t nextCandidateId_;     // 下一个候选ID
    

    struct TargetSignatureInfo2 {
        const MediaItem *mediaItem;
        const SignaturePoint *signaturePoint;  // 直接存储SignaturePoint指针，包含完整信息
        const std::vector<SignaturePoint> *signature;
    };
    std::unordered_map<uint32_t, std::vector<TargetSignatureInfo2>> hash2TargetSignaturesInfoMap_;  // 哈希值到时间戳的映射

    std::shared_ptr<IPerformanceConfig> config_;
    size_t maxCandidates_;         // 最大候选结果数
    size_t maxCandidatesPerSignature_; // 每个signature的最大候选结果数
    double matchExpireTime_;       // 匹配过期时间 (秒)
    float minConfidenceThreshold_; // 最小置信度阈值
    size_t minMatchesRequired_;    // 最小匹配点数要求
    size_t minMatchesUniqueTimestampRequired_; // 最小unique时间戳数量要求
    double offsetTolerance_;       // 时间偏移容忍度 (秒)

    std::unordered_map< const std::vector<SignaturePoint> *, size_t> signature2SessionCnt_;

    struct DebugMatchInfo {
        std::string hash;
        double queryTime;
        double targetTime;
        int32_t offset;
        // 查询点的完整信息
        uint32_t queryFrequency;    
        uint32_t queryAmplitude;    
        // 源点的完整信息（来自目标签名数据库）
        uint32_t sourceFrequency;   
        uint32_t sourceAmplitude;   
    };
    struct MatchingCandidate;
    std::unordered_map<size_t, std::vector<std::pair<size_t, DebugMatchInfo>>> findDuplicateHashes(const std::vector<std::pair<CandidateSessionKey, MatchingCandidate>>& candidates);

    struct MatchingCandidate {
        const TargetSignatureInfo2* targetSignatureInfo; // 目标签名信息
        size_t maxPossibleMatches;          // 最大可能匹配点数 
        size_t matchCount;                  // 匹配点数量
        std::vector<DebugMatchInfo> matchInfos;           // 匹配信息
        double lastMatchTime;               // 最后一次匹配的时间戳
        int32_t offset;                     // 时间偏移（毫秒，用于session key）
        int64_t actualOffsetSum;             // 累积的实际时间偏移（毫秒），使用int64_t防止溢出
        int64_t offsetCount;                 // 偏移计数，用于计算平均值
        size_t uniqueTimestampCount;        // unique时间戳数量
        std::unordered_set<double> uniqueTimestamps; // unique时间戳集合（保留2位小数精度）
        bool isMatchCountChanged;           // 是否匹配点数量发生变化
        bool isNotified;                    // 是否已通知
    };
    std::unordered_map<CandidateSessionKey, MatchingCandidate> session2CandidateMap_;
    std::vector<MatchResult> matchResults_;
    std::vector<CandidateSessionKey> expiredCandidateSessionKeys_;
    
    // Visualization data
    bool collectVisualizationData_ = false;
    VisualizationData visualizationData_;
    
    // 存储整个过程中所有session的历史数据，用于可视化
    // Key: sessionKey的字符串表示, Value: 该session的所有匹配信息
    std::unordered_map<std::string, std::vector<DebugMatchInfo>> allSessionsHistory_;
    
    // Helper method for merging sessions with similar time offsets
    void mergeSimilarSessions();
    
    // 尝试将新的候选session与现有的同signature sessions合并
    // 返回值：如果成功合并返回被合并到的sessionKey，否则返回{0, nullptr}
    CandidateSessionKey tryMergeWithExistingSessions(
        const CandidateSessionKey& newSessionKey, 
        const MatchingCandidate& newCandidate);
    
    // 计算候选session的分数
    double calculateSessionScore(const MatchingCandidate& candidate, double currentTimestamp) const;
    
    // 找到分数最低的session
    CandidateSessionKey findLowestScoreSession(double currentTimestamp) const;
    
    // 检查是否应该替换现有session
    bool shouldReplaceSession(const MatchingCandidate& newCandidate, double currentTimestamp) const;
    
    // 找到指定signature下分数最低的session
    CandidateSessionKey findLowestScoreSessionInSignature(
        const std::vector<SignaturePoint>* signature, double currentTimestamp) const;
    
    // 检查是否应该替换同一signature下的现有session
    bool shouldReplaceSessionInSignature(
        const MatchingCandidate& newCandidate, 
        const std::vector<SignaturePoint>* signature, 
        double currentTimestamp) const;
    
    // 生成sessionKey的字符串表示，用于可视化session ID
    std::string generateSessionId(const CandidateSessionKey& sessionKey) const;
};

} // namespace afp 
