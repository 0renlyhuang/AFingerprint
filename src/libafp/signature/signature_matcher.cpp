#include "signature/signature_matcher.h"
#include "matcher/matcher.h"
#include "debugger/audio_debugger.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <unordered_set>

namespace afp {

size_t SignatureMatcher::nextCandidateId_ = 0;

SignatureMatcher::SignatureMatcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config)
    : catalog_(catalog)
    , config_(config)
    , maxCandidates_(config->getMatchingConfig().maxCandidates)
    , matchExpireTime_(config->getMatchingConfig().matchExpireTime)
    , minConfidenceThreshold_(config->getMatchingConfig().minConfidenceThreshold)
    , minMatchesRequired_(config->getMatchingConfig().minMatchesRequired)
    , offsetTolerance_(config->getMatchingConfig().offsetTolerance) {
    
    // 预处理所有目标签名
    const auto& signatures = catalog_->signatures();
    const auto& mediaItems = catalog_->mediaItems();
    
    for (size_t i = 0; i < signatures.size(); ++i) {
        const auto& signature = signatures[i];
        const auto& mediaItem = mediaItems[i];
        
        if (signature.empty()) {
            std::cerr << "警告: 数据库中的指纹 #" << i << " (" << mediaItem.title() << ") 是空的!" << std::endl;
            continue;
        }
        
        TargetSignatureInfo info;
        info.mediaItem = &mediaItem;
        
        // 为每个哈希值建立时间戳映射
        for (const auto& point : signature) {
            info.hashTimestamps[point.hash].push_back(point.timestamp);
        }
        
        targetSignaturesInfo_.push_back(std::move(info));
        std::cout << "已预处理媒体项: " << mediaItem.title() 
                  << " (哈希值数量: " << info.hashTimestamps.size() << ")" << std::endl;
    }
}

SignatureMatcher::~SignatureMatcher() = default;

void SignatureMatcher::processQuerySignature(
    const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount) {
    
    if (querySignature.empty()) {
        return;
    }
    
    double currentTimestamp = querySignature.back().timestamp;
    
    // 执行匹配
    performMatching(querySignature, inputChannelCount);
    
    // 更新候选结果状态
    updateCandidates(currentTimestamp);
}

void SignatureMatcher::performMatching(const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount) {
    if (querySignature.empty() || targetSignaturesInfo_.empty()) {
        return;
    }
    
    std::cout << "开始匹配过程，查询指纹点数量: " << querySignature.size() << std::endl;
    AudioDebugger::printQuerySignatureStats(querySignature);
    
    // 计算指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueQueryHashes;
    for (const auto& point : querySignature) {
        uniqueQueryHashes.insert(point.hash);
    }
    
    // 对每个目标签名进行匹配
    for (size_t i = 0; i < targetSignaturesInfo_.size(); ++i) {
        const auto& targetInfo = targetSignaturesInfo_[i];
        const auto& mediaItem = *(targetInfo.mediaItem);
        
        // 使用AudioDebugger打印目标指纹统计信息
        AudioDebugger::printTargetSignatureStats(catalog_->signatures()[i], mediaItem.title(), i);
        
        // 计算目标指纹中的唯一哈希值
        std::unordered_set<uint32_t> uniqueTargetHashes;
        for (const auto& [hash, _] : targetInfo.hashTimestamps) {
            uniqueTargetHashes.insert(hash);
        }
        
        // 使用AudioDebugger打印哈希交集信息
        AudioDebugger::printCommonHashesInfo(uniqueQueryHashes, uniqueTargetHashes);
        
        // 比较哈希值
        size_t hashMatches = 0;
        size_t totalTargetHashesCount = targetInfo.hashTimestamps.size();
        
        for (const auto& queryPoint : querySignature) {
            auto targetIt = targetInfo.hashTimestamps.find(queryPoint.hash);
            if (targetIt != targetInfo.hashTimestamps.end()) {
                // 找到匹配的哈希
                for (double targetTime : targetIt->second) {
                    // 处理每个匹配的时间戳
                    processHashMatch(queryPoint.hash, queryPoint.timestamp, mediaItem, targetTime, inputChannelCount, totalTargetHashesCount);
                    
                    // 只记录一次匹配数，即使一个哈希有多个时间戳
                    if (hashMatches == 0 || queryPoint.hash != querySignature[hashMatches-1].hash) {
                        hashMatches++;
                    }
                    
                    // 仅打印前100个匹配详情
                    if (hashMatches <= 100) {
                        std::cout << "  哈希匹配: 0x" << std::hex << queryPoint.hash << std::dec 
                                << " (查询时间: " << queryPoint.timestamp 
                                << "s, 目标时间: " << targetTime << "s)" << std::endl;
                    }
                }
            }
        }
        
        std::cout << "  哈希值匹配数量: " << hashMatches << std::endl;
    }
}

void SignatureMatcher::processHashMatch(uint32_t hash, double queryTime, 
                                      const MediaItem& mediaItem, double targetTime, size_t inputChannelCount, size_t totalTargetHashesCount) {
    
    // 计算时间偏移
    double offset = targetTime - queryTime;
    
    // 查找是否有相同媒体项且偏移接近的候选
    bool foundCandidate = false;
    const double offsetTolerance = offsetTolerance_;
    
    // 查找该媒体项的所有候选结果
    auto mediaItemIt = mediaItemCandidates_.find(&mediaItem);
    if (mediaItemIt != mediaItemCandidates_.end()) {
        for (size_t candidateIdx : mediaItemIt->second) {
            MatchCandidate& candidate = candidates_[candidateIdx];
            
            // 检查时间偏移是否接近
            if (std::abs(candidate.offset - offset) <= offsetTolerance) {
                // 已有相同偏移的候选，更新它
                foundCandidate = true;
                
                // 如果这个哈希值之前没有匹配过，或者当前时间戳更新，则更新哈希表
                auto hashIt = candidate.matchedHashes.find(hash);
                if (hashIt == candidate.matchedHashes.end() || 
                    queryTime > hashIt->second) {
                    candidate.matchedHashes[hash] = queryTime;
                    candidate.matchCount++;
                    candidate.lastMatchTime = queryTime;
                    
                    // 计算置信度
                    double channelRatio = 1.0;
                    if (candidate.channelCount > 0) {
                        // 如果候选音频通道数大于输入音频通道数，则根据通道比例调整最大可匹配特征数
                        channelRatio = std::min(1.0, static_cast<double>(inputChannelCount) / candidate.channelCount);
                    }
                    
                    // 计算考虑通道比例后的最大可匹配特征数
                    size_t maxPossibleMatches = static_cast<size_t>(candidate.totalTargetHashesCount * channelRatio);
                    
                    // 计算置信度
                    if (candidate.matchCount >= minMatchesRequired_) {
                        if (candidate.matchCount >= maxPossibleMatches) {
                            candidate.confidence = 1.0;
                        } else {
                            candidate.confidence = static_cast<double>(candidate.matchCount) / maxPossibleMatches;
                        }
                    } else {
                        if (maxPossibleMatches < minMatchesRequired_) {
                            candidate.confidence = static_cast<double>(candidate.matchCount) / minMatchesRequired_;
                        } else {
                            candidate.confidence = 0.0;
                        }
                    }
                    
                    // 当置信度较高时打印详细信息
                    if (candidate.confidence >= 0.2 && candidate.matchCount % 5 == 0) {
                        std::cout << "候选更新: [" << mediaItem.title() 
                                  << "] ID=" << candidate.id
                                  << ", 偏移=" << std::fixed << std::setprecision(2) << candidate.offset
                                  << "s, 匹配点=" << candidate.matchCount
                                  << ", 置信度=" << std::setprecision(2) << candidate.confidence
                                  << ", 最后匹配时间=" << std::setprecision(2) << candidate.lastMatchTime
                                  << "s" << std::endl;
                    }
                    
                    // 评估候选结果
                    evaluateCandidate(candidate, queryTime);
                }
                break;
            }
        }
    }
    
    // 如果没有找到匹配的候选结果，创建一个新的
    if (!foundCandidate) {
        MatchCandidate newCandidate;
        newCandidate.mediaItem = &mediaItem;
        newCandidate.offset = offset;
        newCandidate.confidence = 0.0;
        newCandidate.matchCount = 1;
        newCandidate.lastMatchTime = queryTime;
        newCandidate.matchedHashes[hash] = queryTime;
        newCandidate.id = nextCandidateId_++;
        newCandidate.channelCount = mediaItem.channelCount();  // 存储候选音频的通道数
        newCandidate.totalTargetHashesCount = totalTargetHashesCount;
        
        // 添加新候选
        size_t newIdx = candidates_.size();
        candidates_.push_back(newCandidate);
        
        // 更新映射
        mediaItemCandidates_[&mediaItem].push_back(newIdx);
        
        // 限制候选结果数量
        limitCandidatesCount();
        
        std::cout << "新候选: [" << mediaItem.title() 
                  << "] ID=" << newCandidate.id
                  << ", 偏移=" << std::fixed << std::setprecision(2) << offset
                  << "s" << std::endl;
    }
}

void SignatureMatcher::updateCandidates(double currentTimestamp) {
    // 淘汰过期的候选结果
    removeExpiredCandidates(currentTimestamp);
    
    // 限制候选结果数量
    limitCandidatesCount();
    
    // 评估所有候选结果
    for (auto& candidate : candidates_) {
        evaluateCandidate(candidate, currentTimestamp);
    }
}

bool SignatureMatcher::evaluateCandidate(MatchCandidate& candidate, double currentTimestamp) {
    // 如果达到阈值，生成匹配结果并通知
    if (candidate.confidence >= minConfidenceThreshold_ && 
        candidate.matchCount >= minMatchesRequired_) {
        
        // 只有在有回调函数的情况下才通知
        if (matchNotifyCallback_) {
            // 构建匹配点集
            std::vector<SignaturePoint> matchedPoints;
            for (const auto& [hash, timestamp] : candidate.matchedHashes) {
                SignaturePoint point;
                point.hash = hash;
                point.timestamp = timestamp;
                // 其他字段在这里不重要，保持默认值
                matchedPoints.push_back(point);
            }
            
            // 创建匹配结果
            MatchResult result{
                *(candidate.mediaItem),
                candidate.offset,
                candidate.confidence,
                matchedPoints,
                candidate.id
            };
            
            // 通知回调
            matchNotifyCallback_(result);
            
            // 打印匹配成功信息
            std::cout << "===== 匹配成功 =====" << std::endl;
            std::cout << "ID: " << candidate.id << std::endl;
            std::cout << "媒体项: " << candidate.mediaItem->title() << std::endl;
            std::cout << "时间偏移: " << std::fixed << std::setprecision(2) << candidate.offset << "s" << std::endl;
            std::cout << "置信度: " << std::setprecision(2) << candidate.confidence << std::endl;
            std::cout << "匹配点数: " << candidate.matchCount << std::endl;
            std::cout << "===================" << std::endl;
            
            // 可选：匹配成功后清除该候选，避免重复通知
            // 但这会导致无法继续积累更多匹配点，权衡利弊
            // 这里我们选择保留候选，但可以根据需要调整
            
            return true;
        }
    }
    
    return false;
}

void SignatureMatcher::removeExpiredCandidates(double currentTimestamp) {
    std::vector<MatchCandidate> activeCandidates;
    std::unordered_map<const MediaItem*, std::vector<size_t>> activeMediaItemCandidates;
    
    for (const auto& candidate : candidates_) {
        // 检查是否过期
        if (currentTimestamp - candidate.lastMatchTime <= matchExpireTime_) {
            // 未过期，保留
            size_t newIdx = activeCandidates.size();
            activeCandidates.push_back(candidate);
            activeMediaItemCandidates[candidate.mediaItem].push_back(newIdx);
        } else {
            // 打印过期信息
            std::cout << "候选过期: [" << candidate.mediaItem->title() 
                      << "] 偏移=" << std::fixed << std::setprecision(2) << candidate.offset
                      << "s, 匹配点=" << candidate.matchCount
                      << ", 最后活跃: " << std::setprecision(2) 
                      << (currentTimestamp - candidate.lastMatchTime) << "秒前" << std::endl;
        }
    }
    
    // 更新候选结果集
    candidates_ = std::move(activeCandidates);
    mediaItemCandidates_ = std::move(activeMediaItemCandidates);
}

void SignatureMatcher::limitCandidatesCount() {
    if (candidates_.size() <= maxCandidates_) {
        return; // 不需要淘汰
    }
    
    // 按最后匹配时间排序（降序）
    std::sort(candidates_.begin(), candidates_.end(), 
              [](const MatchCandidate& a, const MatchCandidate& b) {
                  return a.lastMatchTime > b.lastMatchTime;
              });
    
    // 只保留前 maxCandidates 个
    candidates_.resize(maxCandidates_);
    
    // 重建媒体项映射
    mediaItemCandidates_.clear();
    for (size_t i = 0; i < candidates_.size(); ++i) {
        mediaItemCandidates_[candidates_[i].mediaItem].push_back(i);
    }
    
    std::cout << "候选数量达到上限，淘汰了 " 
              << (candidates_.size() - maxCandidates_) << " 个候选" << std::endl;
}

} // namespace afp 