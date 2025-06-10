#include "signature/signature_matcher.h"
#include "matcher/matcher.h"
#include "debugger/audio_debugger.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <unordered_set>
#include <sstream>


namespace afp {


std::unordered_map<size_t, std::vector<std::pair<size_t, SignatureMatcher::DebugMatchInfo>>> SignatureMatcher::findDuplicateHashes(
    const std::vector<std::pair<CandidateSessionKey, MatchingCandidate>>& candidates) {
    
    std::unordered_map<size_t, std::vector<std::pair<size_t, DebugMatchInfo>>> result;
    
    // 遍历所有候选项
    for (size_t candidateIdx = 0; candidateIdx < candidates.size(); ++candidateIdx) {
        const auto& candidate = candidates[candidateIdx].second;
        
        // 用于检测重复的哈希+偏移量组合
        std::unordered_map<std::string, std::vector<size_t>> hashOffsetPositions;
        
        // 首先记录每个哈希值和偏移量组合出现的位置
        for (size_t infoIdx = 0; infoIdx < candidate.matchInfos.size(); ++infoIdx) {
            const auto& matchInfo = candidate.matchInfos[infoIdx];
            // 创建哈希值和偏移量的组合键
            std::string hashOffsetKey = matchInfo.hash + "_" + std::to_string(matchInfo.offset);
            hashOffsetPositions[hashOffsetKey].push_back(infoIdx);
        }
        
        // 找出重复的哈希值和偏移量组合
        for (const auto& [hashOffsetKey, positions] : hashOffsetPositions) {
            if (positions.size() > 1) {  // 有重复
                // 只有在发现重复时才添加此候选项到结果
                if (result.find(candidateIdx) == result.end()) {
                    result[candidateIdx] = {};
                }
                
                // 记录所有重复的实例
                for (size_t pos : positions) {
                    result[candidateIdx].emplace_back(pos, candidate.matchInfos[pos]);
                }
            }
        }
    }
    
    return result;
}

template <typename T>
std::string hexHashString(const T& value) {
    std::stringstream ss;
    ss << " 0x" << std::hex << value;
    return ss.str();
}

size_t SignatureMatcher::nextCandidateId_ = 0;

// SignatureMatcher::SignatureMatcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config)
//     : catalog_(catalog)
//     , config_(config)
//     , maxCandidates_(config->getMatchingConfig().maxCandidates)
//     , matchExpireTime_(config->getMatchingConfig().matchExpireTime)
//     , minConfidenceThreshold_(config->getMatchingConfig().minConfidenceThreshold)
//     , minMatchesRequired_(config->getMatchingConfig().minMatchesRequired)
//     , offsetTolerance_(config->getMatchingConfig().offsetTolerance) {
    
//     // 预处理所有目标签名
//     const auto& signatures = catalog_->signatures();
//     const auto& mediaItems = catalog_->mediaItems();
    
//     for (size_t i = 0; i < signatures.size(); ++i) {
//         const auto& signature = signatures[i];
//         const auto& mediaItem = mediaItems[i];
        
//         if (signature.empty()) {
//             std::cerr << "警告: 数据库中的指纹 #" << i << " (" << mediaItem.title() << ") 是空的!" << std::endl;
//             continue;
//         }
        
//         TargetSignatureInfo info;
//         info.mediaItem = &mediaItem;
        
//         // 为每个哈希值建立时间戳映射
//         for (const auto& point : signature) {
//             info.hashTimestamps[point.hash].push_back(point.timestamp);
//         }
        
//         targetSignaturesInfo_.push_back(std::move(info));
//         std::cout << "已预处理媒体项: " << mediaItem.title() 
//                   << " (哈希值数量: " << info.hashTimestamps.size() << ")" << std::endl;
//     }
// }

SignatureMatcher::SignatureMatcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config)
    : catalog_(catalog)
    , config_(config)
    , maxCandidates_(config->getMatchingConfig().maxCandidates)
    , maxCandidatesPerSignature_(config->getMatchingConfig().maxCandidatesPerSignature)
    , matchExpireTime_(config->getMatchingConfig().matchExpireTime)
    , minConfidenceThreshold_(config->getMatchingConfig().minConfidenceThreshold)
    , minMatchesRequired_(config->getMatchingConfig().minMatchesRequired)
    , minMatchesUniqueTimestampRequired_(config->getMatchingConfig().minMatchesUniqueTimestampRequired)
    , offsetTolerance_(config->getMatchingConfig().offsetTolerance)
    , matchResults_(std::vector<MatchResult>(config->getMatchingConfig().maxCandidates))
    , expiredCandidateSessionKeys_(std::vector<CandidateSessionKey>(config->getMatchingConfig().maxCandidates)) {
    
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

        // todo: 性能提升手段：
        // 对于单个hash值，不需要vector存储，直接存储
        // 对于多个hash值，需要vector存储
        for (const auto& point : signature) {
            TargetSignatureInfo2 info = {&mediaItem, &point, &signature};
            if (hash2TargetSignaturesInfoMap_.find(point.hash) != hash2TargetSignaturesInfoMap_.end()) {
                hash2TargetSignaturesInfoMap_[point.hash].push_back(std::move(info));
            } else {
                hash2TargetSignaturesInfoMap_[point.hash] = std::vector<TargetSignatureInfo2>{std::move(info)};
            }
        }

        // 按所有point的时间排序，输出前100个point的hash和timestamp
        std::vector<std::pair<uint32_t, double>> points;
        for (const auto& point : signature) {
            points.emplace_back(point.hash, point.timestamp);
        }
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        // std::cout << "rrr target: 按所有point的时间排序，输出前100个point的hash和timestamp" << std::endl;
        // for (size_t i = 0; i < std::min(points.size(), size_t(300)); ++i) {
        //     std::cout << "rrr  [" << i + 1 << "] hash: 0x" << std::hex << points[i].first << std::dec 
        //               << ", timestamp: " << points[i].second << std::endl;
        // }
    }

    std::cout << "预处理所有目标签名完成"
              << " (signature数量: " << signatures.size() << ")"
              << " (唯一哈希值数量: " << hash2TargetSignaturesInfoMap_.size() << ")" << std::endl;
}

SignatureMatcher::~SignatureMatcher() = default;



void SignatureMatcher::processQuerySignature(
    const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount) {
    
    // 计算实际时间偏移（不量化）
    auto calculate_actual_offset = [](double queryTime, double targetTime) -> int32_t {
        return static_cast<int32_t>((queryTime - targetTime) * 1000) ;  // 返回实际时间差(毫秒)
    };
    


    auto hash_seesion_key_func = [](const afp::CandidateSessionKey& k) {
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
    };


    if (querySignature.empty()) {
        return;
    }
    if (hash2TargetSignaturesInfoMap_.empty()) {
        return;
    }
    
    // Reset visualization data if collection is enabled
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
        visualizationData_.title = "Query Audio";
        
        // Add all query points to visualization data
        for (const auto& point : querySignature) {
            visualizationData_.fingerprintPoints.emplace_back(
                point.frequency, point.timestamp, point.hash);
            
            // Also add as general peak for better visualization
            visualizationData_.allPeaks.emplace_back(point.frequency, point.timestamp, point.amplitude / 1000.0f);
        }
        
        // Set duration to the last timestamp + buffer
        if (!querySignature.empty()) {
            visualizationData_.duration = querySignature.back().timestamp + 1.0;
        }
    }
    
    int queryPointprint = 0;
    int queryPointHitCount = 0;

    // step1 add/update candidate using actual time offsets
    for (const auto& queryPoint : querySignature) {
        ++queryPointprint;

        auto targetIt = hash2TargetSignaturesInfoMap_.find(queryPoint.hash);
        if (targetIt == hash2TargetSignaturesInfoMap_.end()) {
            continue;
        }
        ++queryPointHitCount;

        const auto& targetSignaturesInfoList = targetIt->second;
        for (const auto& targetSignaturesInfo : targetSignaturesInfoList) {
            // 计算实际时间偏移
            const auto actualOffset = calculate_actual_offset(queryPoint.timestamp, targetSignaturesInfo.signaturePoint->timestamp);

            const auto sessionKey = CandidateSessionKey{
                .offset = actualOffset,
                .signature = targetSignaturesInfo.signature
            };

            const auto foundCandidate = session2CandidateMap_.find(sessionKey) != session2CandidateMap_.end();
            if (foundCandidate) {
                auto& candidate = session2CandidateMap_[sessionKey];
                if (candidate.isNotified) {
                    continue;
                }
                if (queryPoint.timestamp >= candidate.lastMatchTime || true) {  // todo: 当前不考虑时间戳，直接更新match count
                    candidate.matchCount += 1;
                    
                    // 更新unique时间戳
                    double roundedTime = std::round(queryPoint.timestamp * 100.0) / 100.0;
                    bool isNewTimestamp = candidate.uniqueTimestamps.insert(roundedTime).second;
                    if (isNewTimestamp) {
                        candidate.uniqueTimestampCount += 1;
                    }
                    
                    // 直接使用targetSignaturesInfo.signaturePoint中的完整信息
                    const SignaturePoint* sourcePoint = targetSignaturesInfo.signaturePoint;
                    
                    candidate.matchInfos.push_back(DebugMatchInfo { 
                        hexHashString(queryPoint.hash), 
                        queryPoint.timestamp, 
                        targetSignaturesInfo.signaturePoint->timestamp, 
                        actualOffset, 
                        queryPoint.frequency, 
                        queryPoint.amplitude,
                        sourcePoint->frequency,
                        sourcePoint->amplitude,
                        *sourcePoint
                    });
                    candidate.lastMatchTime = queryPoint.timestamp;
                    candidate.isMatchCountChanged = true;
                    
                    // 累积实际偏移
                    candidate.actualOffsetSum += actualOffset;
                    candidate.offsetCount += 1;

                    // std::cout << "rrr update candidate: " << queryPointprint << " hash: 0x" << std::hex << queryPoint.hash << std::dec 
                    // << ", timestamp: " << queryPoint.timestamp 
                    // << ", targetSignaturesInfo.signaturePoint->timestamp: " << targetSignaturesInfo.signaturePoint->timestamp 
                    // << ", actualOffset: " << actualOffset 
                    // << ", sessionKey: " << hash_seesion_key_func(sessionKey) 
                    // << ", matchcount: " << candidate.matchCount 
                    // << ", uniqueTimestampCount: " << candidate.uniqueTimestampCount
                    // << ", actualOffsetSum: " << candidate.actualOffsetSum
                    // << ", offsetCount: " << candidate.offsetCount
                    // << ", averageOffset: " << candidate.actualOffsetSum / candidate.offsetCount 
                    // << std::endl;

                    // 存储到session历史记录中，用于可视化
                    if (collectVisualizationData_) {
                        std::string sessionId = generateSessionId(sessionKey);
                        allSessionsHistory_[sessionId].push_back(
                            DebugMatchInfo { 
                                hexHashString(queryPoint.hash), 
                                queryPoint.timestamp, 
                                targetSignaturesInfo.signaturePoint->timestamp, 
                                actualOffset, 
                                queryPoint.frequency, 
                                queryPoint.amplitude,
                                sourcePoint->frequency,
                                sourcePoint->amplitude,
                                *sourcePoint
                            });
                    }

                    continue;
                } 
            } else {
                // 创建新的候选项
                double channelRatio = 1.0;
                const auto targetChannelCount = targetSignaturesInfo.mediaItem->channelCount();
                if (targetChannelCount > 0) {
                    // 如果候选音频通道数大于输入音频通道数，则根据通道比例调整最大可匹配特征数
                    channelRatio = std::min(1.0, static_cast<double>(inputChannelCount) / targetChannelCount);
                }
                // 计算考虑通道比例后的最大可匹配特征数
                const auto targetHashesCount = targetSignaturesInfo.signature->size();
                const auto maxPossibleMatches = static_cast<size_t>(targetHashesCount * channelRatio);

                // 查找源SignaturePoint以获取完整信息（源点就是目标签名中的匹配点）
                const SignaturePoint* sourcePoint = targetSignaturesInfo.signaturePoint;
                
                // 初始化unique时间戳
                double roundedTime = std::round(queryPoint.timestamp * 100.0) / 100.0;
                std::unordered_set<double> initialUniqueTimestamps = {roundedTime};
                
                MatchingCandidate newCandidate = {
                    .targetSignatureInfo = &targetSignaturesInfo,
                    .maxPossibleMatches = maxPossibleMatches,
                    .matchCount = 1,
                    .matchInfos = { DebugMatchInfo { 
                        hexHashString(queryPoint.hash), 
                        queryPoint.timestamp, 
                        targetSignaturesInfo.signaturePoint->timestamp, 
                        actualOffset, 
                        queryPoint.frequency, 
                        queryPoint.amplitude,
                        sourcePoint->frequency,
                        sourcePoint->amplitude,
                        *sourcePoint
                    } },
                    .lastMatchTime = queryPoint.timestamp,
                    .offset = actualOffset,
                    .actualOffsetSum = actualOffset,  // 初始化累积偏移
                    .offsetCount = 1,                 // 初始化偏移计数
                    .uniqueTimestampCount = 1,        // 初始化unique时间戳数量
                    .uniqueTimestamps = std::move(initialUniqueTimestamps), // 初始化unique时间戳集合
                    .isMatchCountChanged = true,
                    .isNotified = false,
                };
                
                // 第一步：尝试与现有的同signature sessions合并
                CandidateSessionKey mergedSessionKey = tryMergeWithExistingSessions(sessionKey, newCandidate);
                if (mergedSessionKey.signature != nullptr) {
                    // 成功合并到现有session，添加可视化数据
                    if (collectVisualizationData_) {
                        std::string sessionId = generateSessionId(mergedSessionKey);
                        allSessionsHistory_[sessionId].push_back(
                            DebugMatchInfo { 
                                hexHashString(queryPoint.hash), 
                                queryPoint.timestamp, 
                                targetSignaturesInfo.signaturePoint->timestamp, 
                                actualOffset, 
                                queryPoint.frequency, 
                                queryPoint.amplitude,
                                sourcePoint->frequency,
                                sourcePoint->amplitude,
                                *sourcePoint
                            });
                    }
                    
                    if (queryPointprint < 300) {
                        std::cout << "rrr merged into existing session: " << queryPointprint 
                        << " hash: 0x" << std::hex << queryPoint.hash << std::dec 
                        << ", timestamp: " << queryPoint.timestamp 
                        << ", merged into sessionKey: " << hash_seesion_key_func(mergedSessionKey) 
                        << ", new match count: " << session2CandidateMap_[mergedSessionKey].matchCount << std::endl;
                    }
                    continue; // 合并成功，处理下一个hash
                }
                
                // 第二步：合并失败，检查是否需要计分淘汰策略
                bool shouldAddCandidate = true;
                CandidateSessionKey sessionToRemove{0, nullptr};
                
                if (signature2SessionCnt_[targetSignaturesInfo.signature] >= maxCandidatesPerSignature_) {
                    // 使用计分机制决定是否替换同一signature下的现有session
                    if (shouldReplaceSessionInSignature(newCandidate, targetSignaturesInfo.signature, queryPoint.timestamp)) {
                        sessionToRemove = findLowestScoreSessionInSignature(targetSignaturesInfo.signature, queryPoint.timestamp);
                        std::cout << "Replacing low-score session within signature " 
                                  << targetSignaturesInfo.signature << " (offset: " << sessionToRemove.offset << ")" 
                                  << " with new candidate" << std::endl;
                    } else {
                        shouldAddCandidate = false;
                        std::cout << "Ignoring new candidate due to low score compared to existing sessions in signature" << std::endl;
                    }
                }
                else if (session2CandidateMap_.size() >= maxCandidates_) {
                    // 使用计分机制决定是否替换现有session
                    if (shouldReplaceSession(newCandidate, queryPoint.timestamp)) {
                        sessionToRemove = findLowestScoreSession(queryPoint.timestamp);
                        std::cout << "Replacing low-score session for " 
                                  << sessionToRemove.signature << " with new candidate for "
                                  << targetSignaturesInfo.signature << std::endl;
                    } else {
                        shouldAddCandidate = false;
                        std::cout << "Ignoring new candidate due to low score compared to existing sessions" << std::endl;
                    }
                }
                
                if (shouldAddCandidate) {
                    // 如果需要移除旧session，先移除
                    if (sessionToRemove.signature != nullptr) {
                        auto removeIt = session2CandidateMap_.find(sessionToRemove);
                        if (removeIt != session2CandidateMap_.end()) {
                            signature2SessionCnt_[sessionToRemove.signature] -= 1;
                            session2CandidateMap_.erase(removeIt);
                            std::cout << "Removed low-score session: signature=" << sessionToRemove.signature 
                                      << ", offset=" << sessionToRemove.offset << std::endl;
                        }
                    }
                    
                    // 添加新session
                    signature2SessionCnt_[targetSignaturesInfo.signature] += 1;
                    
                    // 存储到session历史记录中，用于可视化
                    if (collectVisualizationData_) {
                        std::string sessionId = generateSessionId(sessionKey);
                        allSessionsHistory_[sessionId].push_back(
                            DebugMatchInfo { 
                                hexHashString(queryPoint.hash), 
                                queryPoint.timestamp, 
                                targetSignaturesInfo.signaturePoint->timestamp, 
                                actualOffset, 
                                queryPoint.frequency, 
                                queryPoint.amplitude,
                                sourcePoint->frequency,
                                sourcePoint->amplitude,
                                *sourcePoint
                            });
                    }

                    session2CandidateMap_[sessionKey] = newCandidate;
                    // if (queryPointprint < 300) {
                        std::cout << "rrr add new candidate: " << queryPointprint << " hash: 0x" << std::hex << queryPoint.hash << std::dec 
                        << ", timestamp: " << queryPoint.timestamp 
                        << ", targetSignaturesInfo.signaturePoint->timestamp: " << targetSignaturesInfo.signaturePoint->timestamp 
                        << ", actualOffset: " << actualOffset 
                        << ", sessionKey: " << hash_seesion_key_func(sessionKey) 
                        << ", matchcount: " << newCandidate.matchCount 
                        << ", uniqueTimestampCount: " << newCandidate.uniqueTimestampCount
                        << ", actualOffsetSum: " << newCandidate.actualOffsetSum
                        << ", offsetCount: " << newCandidate.offsetCount
                        << ", averageOffset: " << newCandidate.actualOffsetSum / newCandidate.offsetCount 
                        << ", score: " << calculateSessionScore(newCandidate, queryPoint.timestamp) << std::endl;
                    // }
                }
            }  
        }
    }
    std::cout << "rrr queryPointHitCount: " << queryPointHitCount << std::endl;

    // step1.5: 全局合并时间偏移在容错范围内的session
    // 注意：现在每个新session都会优先尝试与现有session合并，
    // 这里的全局合并主要处理那些在不同时间点添加但后来发现可以合并的sessions
    mergeSimilarSessions();

    // step2 evaluate candidate
    double currentTimestamp = querySignature.back().timestamp;
    matchResults_.clear();
    expiredCandidateSessionKeys_.clear();

    auto evaluateConfidenceFunc = [this](const MatchingCandidate& candidate) -> double {
        double confidence = 0.0;
        // 计算置信度
        if (candidate.matchCount >= minMatchesRequired_) {
            if (candidate.matchCount >= candidate.maxPossibleMatches) {
                confidence = 1.0;
            } else {
                confidence = static_cast<double>(candidate.matchCount) / candidate.maxPossibleMatches;
            }
        } else {
            if (candidate.maxPossibleMatches < minMatchesRequired_) {
                confidence = static_cast<double>(candidate.matchCount) / minMatchesRequired_;
            } else {
                confidence = 0.0;
            }
        }
        return confidence;
    };
    
    // 输出session2CandidateMap_中match count最大的100个对象的match count
    if (!session2CandidateMap_.empty()) {
        // 创建一个包含所有候选项的向量
        std::vector<std::pair<CandidateSessionKey, MatchingCandidate>> candidates;
        for (const auto& pair : session2CandidateMap_) {
            candidates.push_back(pair);

            std::vector<DebugMatchInfo> matchInfos;
        }
        
        // 按match count降序排序
        std::sort(candidates.begin(), candidates.end(), 
            [](const auto& a, const auto& b) {
                return a.second.matchCount > b.second.matchCount;
            });
        
        // 只输出前100个或全部（如果少于100个）
        size_t outputCount = std::min(candidates.size(), size_t(100));
        std::cout << "Top " << outputCount << " candidates by match count:" << std::endl;
        
        for (size_t i = 0; i < outputCount; ++i) {
            const auto& [key, candidate] = candidates[i];
            double sessionScore = calculateSessionScore(candidate, currentTimestamp);
            std::cout << "  [" << i + 1 << "] MediaItem: " 
                      << candidate.targetSignatureInfo->mediaItem->title()
                      << ", Offset: " << key.offset
                      << ", MatchCount: " << candidate.matchCount
                      << ", uniqueTimestampCount: " << candidate.uniqueTimestampCount
                      << ", MaxPossible: " << candidate.maxPossibleMatches
                      << ", Confidence: " << evaluateConfidenceFunc(candidate)
                      << ", Score: " << std::fixed << std::setprecision(4) << sessionScore
                      << ", LastMatchTime: " << candidate.lastMatchTime
                      << ", sessionKey: " << hash_seesion_key_func(key)
                      << ", averageOffset: " << candidate.actualOffsetSum / candidate.offsetCount
                      << std::endl;
        }
        auto duplicateMatchInfosOfCandidates = findDuplicateHashes(candidates);
        int i = 0;
    }

    


    for (const auto& [sessionKey, candidate] : session2CandidateMap_) {

        if (candidate.isMatchCountChanged && !candidate.isNotified) {
            const auto confidence = evaluateConfidenceFunc(candidate);
            // confidence >= minConfidenceThreshold_ &&  
            if (candidate.matchCount >= minMatchesRequired_) {
                // 检查是否满足unique时间戳数量要求
                if (candidate.uniqueTimestampCount >= minMatchesUniqueTimestampRequired_) {
                    // 计算平均偏移
                    double averageOffset = candidate.actualOffsetSum / candidate.offsetCount;

                    std::vector<SignaturePoint> matchedPoints;
                    for (const auto& matchInfo : candidate.matchInfos) {
                        matchedPoints.push_back(matchInfo.sourcePoint);
                    }
                    
                    auto matchResult = MatchResult{
                        .mediaItem = candidate.targetSignatureInfo->mediaItem,
                        .offset = averageOffset,  // 使用平均偏移（秒）
                        .confidence = confidence,
                        .matchedPoints = matchedPoints,
                        .matchCount = candidate.matchCount,
                        .uniqueTimestampMatchCount = candidate.uniqueTimestampCount,
                        .id = 0,
                    };
                    matchResults_.push_back(matchResult);
                    session2CandidateMap_[sessionKey].isNotified = true;
                    
                    std::cout << "Match accepted: matchCount=" << candidate.matchCount 
                              << ", uniqueTimestamps=" << candidate.uniqueTimestampCount 
                              << ", confidence=" << confidence << std::endl;
                } else {
                    std::cout << "Match rejected due to insufficient unique timestamps: matchCount=" 
                              << candidate.matchCount << ", uniqueTimestamps=" << candidate.uniqueTimestampCount 
                              << ", required=" << minMatchesUniqueTimestampRequired_ << std::endl;
                }
            }
        }


        if (candidate.lastMatchTime + matchExpireTime_ < currentTimestamp) {
            expiredCandidateSessionKeys_.push_back(sessionKey);
        }
    }

    // Setp3 notify match result
    for (const auto& matchResult : matchResults_) {
        matchNotifyCallback_(matchResult);
    }


    // Setp4 remove expired candidate
    for (const auto& expiredCandidateSessionKey : expiredCandidateSessionKeys_) {
        session2CandidateMap_.erase(expiredCandidateSessionKey);
        signature2SessionCnt_[expiredCandidateSessionKey.signature] -= 1;
    }
}

// Merge sessions with similar time offsets within tolerance
void SignatureMatcher::mergeSimilarSessions() {
    if (session2CandidateMap_.empty()) {
        return;
    }
    
    // 按signature分组，只在同一个signature内进行合并
    std::unordered_map<const std::vector<SignaturePoint>*, std::vector<CandidateSessionKey>> signatureGroups;
    
    for (const auto& [sessionKey, candidate] : session2CandidateMap_) {
        signatureGroups[sessionKey.signature].push_back(sessionKey);
    }
    
    // 对每个signature组内的session进行合并
    for (auto& [signature, sessionKeys] : signatureGroups) {
        if (sessionKeys.size() <= 1) {
            continue; // 只有一个session，无需合并
        }
        
        // 按平均偏移排序，便于查找相近的session
        std::sort(sessionKeys.begin(), sessionKeys.end(), 
            [this](const CandidateSessionKey& a, const CandidateSessionKey& b) {
                const auto& candidateA = session2CandidateMap_[a];
                const auto& candidateB = session2CandidateMap_[b];
                
                // 添加安全检查，防止除零错误
                if (candidateA.offsetCount == 0 || candidateB.offsetCount == 0) {
                    std::cerr << "Warning: offsetCount is zero in mergeSimilarSessions!" << std::endl;
                    return false;
                }
                
                double avgOffsetA = static_cast<double>(candidateA.actualOffsetSum) / candidateA.offsetCount;
                double avgOffsetB = static_cast<double>(candidateB.actualOffsetSum) / candidateB.offsetCount;
                return avgOffsetA < avgOffsetB;
            });
        
        // 标记需要删除的session
        std::vector<CandidateSessionKey> sessionsToRemove;
        
        // 从第一个session开始，查找可以合并的session
        for (size_t i = 0; i < sessionKeys.size(); ++i) {
            if (std::find(sessionsToRemove.begin(), sessionsToRemove.end(), sessionKeys[i]) != sessionsToRemove.end()) {
                continue; // 已经被标记删除
            }
            
            auto& primaryCandidate = session2CandidateMap_[sessionKeys[i]];
            
            // 添加安全检查
            if (primaryCandidate.offsetCount == 0) {
                std::cerr << "Warning: Primary candidate offsetCount is zero!" << std::endl;
                continue;
            }
            
            double primaryAvgOffset = static_cast<double>(primaryCandidate.actualOffsetSum) / primaryCandidate.offsetCount;
            
            
            // 查找可以与当前session合并的其他session
            for (size_t j = i + 1; j < sessionKeys.size(); ++j) {
                if (std::find(sessionsToRemove.begin(), sessionsToRemove.end(), sessionKeys[j]) != sessionsToRemove.end()) {
                    continue; // 已经被标记删除
                }
                
                auto& secondaryCandidate = session2CandidateMap_[sessionKeys[j]];
                
                // 添加安全检查
                if (secondaryCandidate.offsetCount == 0) {
                    std::cerr << "Warning: Secondary candidate offsetCount is zero!" << std::endl;
                    continue;
                }
                
                double secondaryAvgOffset = static_cast<double>(secondaryCandidate.actualOffsetSum) / secondaryCandidate.offsetCount;
                
                
                // 检查两个session的平均偏移是否在容错范围内
                double offsetDifference = std::abs(primaryAvgOffset - secondaryAvgOffset);
                double toleranceMs = offsetTolerance_ * 1000.0;
                
                if (offsetDifference <= toleranceMs) {
                    // 合并session前先验证数据合理性
                    int64_t newOffsetSum = primaryCandidate.actualOffsetSum + secondaryCandidate.actualOffsetSum;
                    size_t newOffsetCount = primaryCandidate.offsetCount + secondaryCandidate.offsetCount;
                    double newAvgOffset = static_cast<double>(newOffsetSum) / newOffsetCount;
                    
                    
                    // 合并session
                    primaryCandidate.matchCount += secondaryCandidate.matchCount;
                    primaryCandidate.actualOffsetSum = newOffsetSum;
                    primaryCandidate.offsetCount = newOffsetCount;
                    
                    // 合并unique时间戳
                    for (const auto& timestamp : secondaryCandidate.uniqueTimestamps) {
                        primaryCandidate.uniqueTimestamps.insert(timestamp);
                    }
                    primaryCandidate.uniqueTimestampCount = primaryCandidate.uniqueTimestamps.size();
                    
                    // 合并匹配信息
                    primaryCandidate.matchInfos.insert(
                        primaryCandidate.matchInfos.end(),
                        secondaryCandidate.matchInfos.begin(),
                        secondaryCandidate.matchInfos.end()
                    );
                    
                    // 更新最后匹配时间
                    primaryCandidate.lastMatchTime = std::max(
                        primaryCandidate.lastMatchTime, 
                        secondaryCandidate.lastMatchTime
                    );
                    
                    primaryCandidate.isMatchCountChanged = true;
                    
                    // 标记secondary session为删除
                    sessionsToRemove.push_back(sessionKeys[j]);
                    
                    std::cout << "Merged sessions: primary avg offset = " << primaryAvgOffset 
                              << "ms, secondary avg offset = " << secondaryAvgOffset 
                              << "ms, new avg offset = " << newAvgOffset
                              << "ms, new match count = " << primaryCandidate.matchCount 
                              << ", new unique timestamp count = " << primaryCandidate.uniqueTimestampCount
                              << ", tolerance = " << toleranceMs << "ms" << std::endl;
                }
            }
        }
        
        // 删除被合并的session
        for (const auto& sessionKey : sessionsToRemove) {
            signature2SessionCnt_[sessionKey.signature] -= 1;
            session2CandidateMap_.erase(sessionKey);
        }
        
        if (!sessionsToRemove.empty()) {
            std::cout << "Removed " << sessionsToRemove.size() 
                      << " merged sessions for signature " << signature << std::endl;
        }
    }
}

// 计算候选session的分数
double SignatureMatcher::calculateSessionScore(const MatchingCandidate& candidate, double currentTimestamp) const {
    // 计算各项评分因子
    
    // 1. 匹配密度分数 (0-1)：匹配数量与最大可能匹配数的比值
    double matchDensityScore = 0.0;
    if (candidate.maxPossibleMatches > 0) {
        matchDensityScore = std::min(1.0, static_cast<double>(candidate.matchCount) / candidate.maxPossibleMatches);
    }
    
    // 2. 匹配数量分数 (0-1)：基于匹配数量的对数缩放评分
    double matchCountScore = 0.0;
    if (candidate.matchCount > 0) {
        // 使用对数缩放，避免大数值主导，设置合理的上限
        double normalizedCount = std::min(static_cast<double>(candidate.matchCount), 100.0);
        matchCountScore = std::log(1.0 + normalizedCount) / std::log(101.0);
    }
    
    // 3. 活跃度分数 (0-1)：基于最后匹配时间的新鲜度
    double activityScore = 0.0;
    double timeSinceLastMatch = currentTimestamp - candidate.lastMatchTime;
    if (timeSinceLastMatch >= 0) {
        // 使用指数衰减函数，半衰期设为matchExpireTime的1/3
        double halfLife = matchExpireTime_ / 3.0;
        activityScore = std::exp(-timeSinceLastMatch * std::log(2.0) / halfLife);
    }
    
    // 4. 偏移一致性分数 (0-1)：偏移值的一致性越高分数越高
    double consistencyScore = 1.0;  // 默认满分
    if (candidate.offsetCount > 1) {
        // 计算平均偏移
        double avgOffset = static_cast<double>(candidate.actualOffsetSum) / candidate.offsetCount;
        
        // 计算偏移的标准差（简化计算）
        double variance = 0.0;
        for (const auto& matchInfo : candidate.matchInfos) {
            double diff = matchInfo.offset - avgOffset;
            variance += diff * diff;
        }
        variance /= candidate.offsetCount;
        double stdDev = std::sqrt(variance);
        
        // 将标准差转换为一致性分数 (标准差越小，一致性越高)
        // 使用1000ms作为参考标准差，超过这个值分数会显著下降
        double refStdDev = 1000.0;  // 1秒的毫秒数
        consistencyScore = std::exp(-stdDev / refStdDev);
    }
    
    // 5. 综合分数计算，使用加权平均
    // 权重分配：匹配密度35%，匹配数量25%，活跃度25%，一致性15%
    double totalScore = 0.1 * matchDensityScore + 
                       0.50 * matchCountScore + 
                       0.35 * activityScore + 
                       0.05 * consistencyScore;
    
    return totalScore;
}

// 找到分数最低的session
CandidateSessionKey SignatureMatcher::findLowestScoreSession(double currentTimestamp) const {
    if (session2CandidateMap_.empty()) {
        return CandidateSessionKey{0, nullptr};
    }
    
    auto lowestIt = session2CandidateMap_.begin();
    double lowestScore = calculateSessionScore(lowestIt->second, currentTimestamp);
    
    for (auto it = session2CandidateMap_.begin(); it != session2CandidateMap_.end(); ++it) {
        double score = calculateSessionScore(it->second, currentTimestamp);
        if (score < lowestScore) {
            lowestScore = score;
            lowestIt = it;
        }
    }
    
    return lowestIt->first;
}

// 检查是否应该替换现有session
bool SignatureMatcher::shouldReplaceSession(const MatchingCandidate& newCandidate, double currentTimestamp) const {
    if (session2CandidateMap_.size() < maxCandidates_) {
        return false; // 还有空间，不需要替换
    }
    
    // 计算新候选的分数
    double newScore = calculateSessionScore(newCandidate, currentTimestamp);
    
    // 找到分数最低的现有session
    CandidateSessionKey lowestKey = findLowestScoreSession(currentTimestamp);
    if (lowestKey.signature == nullptr) {
        return false; // 没有找到有效的session
    }
    
    auto lowestIt = session2CandidateMap_.find(lowestKey);
    if (lowestIt == session2CandidateMap_.end()) {
        return false; // session不存在
    }
    
    double lowestScore = calculateSessionScore(lowestIt->second, currentTimestamp);
    
    // 如果新候选的分数显著高于最低分的现有session，则替换
    // 添加一个小的阈值避免频繁替换
    const double replacementThreshold = 0.1;  // 10%的分数差异阈值
    
    return (newScore > lowestScore + replacementThreshold);
}

// 找到指定signature下分数最低的session
CandidateSessionKey SignatureMatcher::findLowestScoreSessionInSignature(
    const std::vector<SignaturePoint>* signature, double currentTimestamp) const {
    
    if (session2CandidateMap_.empty()) {
        return CandidateSessionKey{0, nullptr};
    }
    
    // 找到第一个属于指定signature的session作为初始比较对象
    auto lowestIt = session2CandidateMap_.end();
    for (auto it = session2CandidateMap_.begin(); it != session2CandidateMap_.end(); ++it) {
        if (it->first.signature == signature) {
            lowestIt = it;
            break;
        }
    }
    
    if (lowestIt == session2CandidateMap_.end()) {
        return CandidateSessionKey{0, nullptr}; // 没有找到属于该signature的session
    }
    
    double lowestScore = calculateSessionScore(lowestIt->second, currentTimestamp);
    
    // 继续查找该signature下分数更低的session
    for (auto it = session2CandidateMap_.begin(); it != session2CandidateMap_.end(); ++it) {
        if (it->first.signature == signature) {
            double score = calculateSessionScore(it->second, currentTimestamp);
            if (score < lowestScore) {
                lowestScore = score;
                lowestIt = it;
            }
        }
    }
    
    return lowestIt->first;
}

// 检查是否应该替换同一signature下的现有session
bool SignatureMatcher::shouldReplaceSessionInSignature(
    const MatchingCandidate& newCandidate, 
    const std::vector<SignaturePoint>* signature, 
    double currentTimestamp) const {
    
    // 计算新候选的分数
    double newScore = calculateSessionScore(newCandidate, currentTimestamp);
    
    // 找到该signature下分数最低的现有session
    CandidateSessionKey lowestKey = findLowestScoreSessionInSignature(signature, currentTimestamp);
    if (lowestKey.signature == nullptr) {
        return false; // 没有找到有效的session
    }
    
    auto lowestIt = session2CandidateMap_.find(lowestKey);
    if (lowestIt == session2CandidateMap_.end()) {
        return false; // session不存在
    }
    
    double lowestScore = calculateSessionScore(lowestIt->second, currentTimestamp);
    
    // 如果新候选的分数显著高于该signature下最低分的现有session，则替换
    // 使用相同的阈值避免频繁替换
    const double replacementThreshold = 0.1;  // 10%的分数差异阈值
    
    return (newScore > lowestScore + replacementThreshold);
}

// Add after the constructor implementations

// Save visualization data to file
bool SignatureMatcher::saveVisualization(const std::string& filename) const {
    if (!collectVisualizationData_) {
        std::cerr << "Visualization data collection is not enabled" << std::endl;
        return false;
    }
    
    // 创建可视化数据的副本，重新生成matchedPoints
    VisualizationData finalVisualizationData = visualizationData_;
    finalVisualizationData.matchedPoints.clear();
    
    // 基于allSessionsHistory_重新生成matchedPoints
    for (const auto& [sessionId, matchInfos] : allSessionsHistory_) {
        for (const auto& matchInfo : matchInfos) {
            // 解析hash字符串
            uint32_t hash = 0;
            if (matchInfo.hash.find("0x") != std::string::npos) {
                std::stringstream ss;
                ss << std::hex << matchInfo.hash.substr(matchInfo.hash.find("0x") + 2);
                ss >> hash;
            }
            
            // 直接使用DebugMatchInfo中的信息，不需要手动查询
            // 使用sessionId的hash值作为session标识
            std::cout << "sessionId: " << sessionId 
            << ", matchInfo.queryFrequency: " << matchInfo.queryFrequency
            << ", matchInfo.queryTime: " << matchInfo.queryTime
            << ", hash: " << hash
            << ", filename: " << filename
            << std::endl;
            uint32_t sessionIdHash = std::hash<std::string>{}(sessionId);
            finalVisualizationData.matchedPoints.push_back(
                std::make_tuple(matchInfo.queryFrequency, matchInfo.queryTime, hash, sessionIdHash)
            );
        }
    }
    
    std::cout << "Generated " << finalVisualizationData.matchedPoints.size() 
              << " matched points from " << allSessionsHistory_.size() << " sessions" << std::endl;
    
    // Save data to JSON (no Python script generation)
    return Visualizer::saveVisualization(finalVisualizationData, filename);
}

// Save sessions data for visualization
bool SignatureMatcher::saveSessionsData(const std::string& filename) const {
    if (!collectVisualizationData_) {
        std::cerr << "Visualization data collection is not enabled" << std::endl;
        return false;
    }
    
    // Extract top sessions (limited to top 3)
    std::vector<SessionData> topSessions;
    
    // Create a vector of all candidates for sorting
    std::vector<std::pair<CandidateSessionKey, MatchingCandidate>> candidates;
    for (const auto& pair : session2CandidateMap_) {
        candidates.push_back(pair);
    }
    
    // Sort by match count in descending order
    std::sort(candidates.begin(), candidates.end(), 
        [](const auto& a, const auto& b) {
            return a.second.matchCount > b.second.matchCount;
        });
    
    // Take top 3 sessions or fewer if less are available
    size_t sessionsToInclude = std::min(candidates.size(), size_t(5));
    for (size_t i = 0; i < sessionsToInclude; ++i) {
        const auto& [key, candidate] = candidates[i];
        
        SessionData sessionData;
        sessionData.id = i + 1; // Use 1-based index for session ID
        sessionData.matchCount = candidate.matchCount;
        
        // Calculate confidence
        double confidence = 0.0;
        if (candidate.matchCount >= minMatchesRequired_) {
            if (candidate.matchCount >= candidate.maxPossibleMatches) {
                confidence = 1.0;
            } else {
                confidence = static_cast<double>(candidate.matchCount) / candidate.maxPossibleMatches;
            }
        }
        sessionData.confidence = confidence;
        
        // Extract media title
        sessionData.mediaTitle = candidate.targetSignatureInfo->mediaItem->title();
        
        topSessions.push_back(sessionData);
    }
    
    // Save sessions data
    return Visualizer::saveSessionsData(topSessions, filename);
}

// Save comparison visualization data
bool SignatureMatcher::saveComparisonData(const VisualizationData& sourceData, 
                                        const std::string& sourceFilename,
                                        const std::string& queryFilename,
                                        const std::string& sessionsFilename) const {
    if (!collectVisualizationData_) {
        std::cerr << "Visualization data collection is not enabled" << std::endl;
        return false;
    }
    
    // Create a copy of the source data to enhance with matched points
    VisualizationData enhancedSourceData = sourceData;
    
    // 基于allSessionsHistory_重新构建session统计信息
    struct SessionStats {
        std::string sessionId;
        size_t matchCount;
        double confidence;
        std::string mediaTitle;
        std::vector<DebugMatchInfo> matchInfos;
        double lastMatchTime;
        size_t uniqueTimestampCount;
        std::unordered_set<double> uniqueTimestamps;
        size_t maxPossibleMatches = 100; // 默认值，后续会从活跃session中更新
    };
    
    std::vector<SessionStats> allSessionStats;
    
    // 首先建立sessionId到活跃MatchingCandidate的映射，用于补充媒体信息
    std::unordered_map<std::string, const MatchingCandidate*> sessionIdToActiveCandidate;
    for (const auto& [sessionKey, candidate] : session2CandidateMap_) {
        std::string sessionId = generateSessionId(sessionKey);
        sessionIdToActiveCandidate[sessionId] = &candidate;
    }
    
    // 从allSessionsHistory_中重建每个session的统计信息
    for (const auto& [sessionId, matchInfos] : allSessionsHistory_) {
        if (matchInfos.empty()) {
            continue;
        }
        
        SessionStats stats;
        stats.sessionId = sessionId;
        stats.matchCount = matchInfos.size();
        stats.matchInfos = matchInfos;
        stats.lastMatchTime = 0.0;
        
        // 计算unique时间戳
        for (const auto& matchInfo : matchInfos) {
            double roundedTime = std::round(matchInfo.queryTime * 100.0) / 100.0;
            stats.uniqueTimestamps.insert(roundedTime);
            stats.lastMatchTime = std::max(stats.lastMatchTime, matchInfo.queryTime);
        }
        stats.uniqueTimestampCount = stats.uniqueTimestamps.size();
        
        // 尝试从活跃候选中获取媒体信息
        auto activeIt = sessionIdToActiveCandidate.find(sessionId);
        if (activeIt != sessionIdToActiveCandidate.end()) {
            const auto* activeCandidate = activeIt->second;
            stats.mediaTitle = activeCandidate->targetSignatureInfo->mediaItem->title();
            stats.maxPossibleMatches = activeCandidate->maxPossibleMatches;
            
            // 使用活跃候选的精确置信度计算
            if (stats.matchCount >= minMatchesRequired_) {
                if (stats.matchCount >= stats.maxPossibleMatches) {
                    stats.confidence = 1.0;
                } else {
                    stats.confidence = static_cast<double>(stats.matchCount) / stats.maxPossibleMatches;
                }
            } else {
                if (stats.maxPossibleMatches < minMatchesRequired_) {
                    stats.confidence = static_cast<double>(stats.matchCount) / minMatchesRequired_;
                } else {
                    stats.confidence = 0.0;
                }
            }
        } else {
            // session已过期，尝试从sessionId解析信息或使用默认值
            stats.mediaTitle = "Expired Session (ID: " + sessionId + ")";
            
            // 使用简化的置信度计算
            if (stats.matchCount >= minMatchesRequired_) {
                stats.confidence = std::min(1.0, static_cast<double>(stats.matchCount) / 50.0); // 假设50为参考值
            } else {
                stats.confidence = 0.0;
            }
        }
        
        allSessionStats.push_back(stats);
    }
    
    // 按匹配数量降序排序
    std::sort(allSessionStats.begin(), allSessionStats.end(), 
        [](const SessionStats& a, const SessionStats& b) {
            return a.matchCount > b.matchCount;
        });
    
    // Take top 5 sessions
    std::vector<SessionData> topSessions;
    size_t sessionsToInclude = std::min(allSessionStats.size(), size_t(5));
    
    // Collect all matched points from query data, assigning session IDs
    VisualizationData sessionQueryData = visualizationData_;
    sessionQueryData.matchedPoints.clear(); // Clear existing matched points
    
    // For each top session, assign IDs to the matched points
    for (size_t i = 0; i < sessionsToInclude; ++i) {
        const auto& sessionStats = allSessionStats[i];
        uint32_t sessionIndex = i + 1; // Use 1-based index for compatibility
        
        // Add session to top sessions list
        SessionData sessionData;
        sessionData.id = sessionIndex;
        sessionData.matchCount = sessionStats.matchCount;
        sessionData.confidence = sessionStats.confidence;
        sessionData.mediaTitle = sessionStats.mediaTitle;
        topSessions.push_back(sessionData);
        
        // 基于该session的匹配信息生成可视化点
        for (const auto& matchInfo : sessionStats.matchInfos) {
            // 解析hash字符串
            uint32_t hash = 0;
            if (matchInfo.hash.find("0x") != std::string::npos) {
                std::stringstream ss;
                ss << std::hex << matchInfo.hash.substr(matchInfo.hash.find("0x") + 2);
                ss >> hash;
            }
            
            // 直接使用DebugMatchInfo中的查询点信息生成查询数据匹配点
            sessionQueryData.matchedPoints.push_back(
                std::make_tuple(matchInfo.queryFrequency, matchInfo.queryTime, hash, sessionIndex)
            );
            
            // 直接使用DebugMatchInfo中的源点信息生成源数据匹配点
            enhancedSourceData.matchedPoints.push_back(
                std::make_tuple(matchInfo.sourceFrequency, matchInfo.targetTime, hash, sessionIndex)
            );
        }
    }
    
    // 确保音频文件路径被保留在对应的可视化数据中
    // 查询数据使用当前可视化数据中的音频路径
    sessionQueryData.audioFilePath = visualizationData_.audioFilePath;
    // 源数据保留其原有的音频路径
    
    // Save source data, query data, and sessions data
    bool success = Visualizer::saveVisualization(enhancedSourceData, sourceFilename) &&
                   Visualizer::saveVisualization(sessionQueryData, queryFilename) &&
                   Visualizer::saveSessionsData(topSessions, sessionsFilename);
    
    if (success) {
        std::cout << "Saved comparison visualization data (based on complete session history):" << std::endl;
        std::cout << "  - Total sessions in history: " << allSessionsHistory_.size() << std::endl;
        std::cout << "  - Active sessions: " << sessionIdToActiveCandidate.size() << std::endl;
        std::cout << "  - Expired sessions: " << (allSessionsHistory_.size() - sessionIdToActiveCandidate.size()) << std::endl;
        std::cout << "  - Top sessions selected: " << sessionsToInclude << std::endl;
        std::cout << "  - Source data: " << sourceFilename << (enhancedSourceData.audioFilePath.empty() ? "" : " (with audio path)") << std::endl;
        std::cout << "  - Query data: " << queryFilename << (sessionQueryData.audioFilePath.empty() ? "" : " (with audio path)") << std::endl;
        std::cout << "  - Sessions data: " << sessionsFilename << std::endl;
    }
    
    return success;
}

// 尝试将新的候选session与现有的同signature sessions合并
CandidateSessionKey SignatureMatcher::tryMergeWithExistingSessions(
    const CandidateSessionKey& newSessionKey, 
    const MatchingCandidate& newCandidate) {
    
    // 只在同一个signature内查找可合并的session
    std::vector<CandidateSessionKey> candidateKeys;
    for (const auto& [sessionKey, candidate] : session2CandidateMap_) {
        if (sessionKey.signature == newSessionKey.signature) {
            candidateKeys.push_back(sessionKey);
        }
    }
    
    if (candidateKeys.empty()) {
        return CandidateSessionKey{0, nullptr}; // 没有同signature的session
    }
    
    // 计算新session的平均偏移
    if (newCandidate.offsetCount == 0) {
        std::cerr << "Warning: New candidate offsetCount is zero!" << std::endl;
        return CandidateSessionKey{0, nullptr};
    }
    
    double newAvgOffset = static_cast<double>(newCandidate.actualOffsetSum) / newCandidate.offsetCount;
    
    double toleranceMs = offsetTolerance_ * 1000.0;
    
    // 查找可以合并的现有session
    for (const auto& existingKey : candidateKeys) {
        auto& existingCandidate = session2CandidateMap_[existingKey];
        
        // 安全检查
        if (existingCandidate.offsetCount == 0) {
            std::cerr << "Warning: Existing candidate offsetCount is zero!" << std::endl;
            continue;
        }
        
        double existingAvgOffset = static_cast<double>(existingCandidate.actualOffsetSum) / existingCandidate.offsetCount;
        
        // 检查两个session的平均偏移是否在容错范围内
        double offsetDifference = std::abs(newAvgOffset - existingAvgOffset);
        
        if (offsetDifference <= toleranceMs) {
            // 可以合并，先验证合并后的数据合理性
            int64_t mergedOffsetSum = existingCandidate.actualOffsetSum + newCandidate.actualOffsetSum;
            size_t mergedOffsetCount = existingCandidate.offsetCount + newCandidate.offsetCount;
            double mergedAvgOffset = static_cast<double>(mergedOffsetSum) / mergedOffsetCount;
            
            // 执行合并
            existingCandidate.matchCount += newCandidate.matchCount;
            existingCandidate.actualOffsetSum = mergedOffsetSum;
            existingCandidate.offsetCount = mergedOffsetCount;
            
            // 合并unique时间戳
            for (const auto& timestamp : newCandidate.uniqueTimestamps) {
                existingCandidate.uniqueTimestamps.insert(timestamp);
            }
            existingCandidate.uniqueTimestampCount = existingCandidate.uniqueTimestamps.size();
            
            // 合并匹配信息
            existingCandidate.matchInfos.insert(
                existingCandidate.matchInfos.end(),
                newCandidate.matchInfos.begin(),
                newCandidate.matchInfos.end()
            );
            
            // 更新最后匹配时间
            existingCandidate.lastMatchTime = std::max(
                existingCandidate.lastMatchTime, 
                newCandidate.lastMatchTime
            );
            
            existingCandidate.isMatchCountChanged = true;
            
            std::cout << "Merged new session into existing: new avg offset = " << newAvgOffset 
                      << "ms, existing avg offset = " << existingAvgOffset 
                      << "ms, merged avg offset = " << mergedAvgOffset
                      << "ms, merged match count = " << existingCandidate.matchCount 
                      << ", new unique timestamp count = " << existingCandidate.uniqueTimestampCount
                      << ", tolerance = " << toleranceMs << "ms" << std::endl;
            
            return existingKey; // 返回被合并到的session key
        }
    }
    
    return CandidateSessionKey{0, nullptr}; // 没有找到可合并的session
}

// 生成sessionKey的字符串表示，用于可视化session ID
std::string SignatureMatcher::generateSessionId(const CandidateSessionKey& sessionKey) const {
    // 使用offset和signature地址拼接生成唯一ID
    std::stringstream ss;
    ss << "s_" << sessionKey.offset << "_" << reinterpret_cast<uintptr_t>(sessionKey.signature);
    return ss.str();
}

} // namespace afp 
