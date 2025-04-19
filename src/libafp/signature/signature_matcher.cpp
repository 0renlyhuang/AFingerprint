#include "signature/signature_matcher.h"
#include "matcher/matcher.h"
#include "debugger/audio_debugger.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <unordered_set>


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
    , matchExpireTime_(config->getMatchingConfig().matchExpireTime)
    , minConfidenceThreshold_(config->getMatchingConfig().minConfidenceThreshold)
    , minMatchesRequired_(config->getMatchingConfig().minMatchesRequired)
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
            TargetSignatureInfo2 info = {&mediaItem, point.timestamp, &signature};
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
        std::cout << "rrr target: 按所有point的时间排序，输出前100个point的hash和timestamp" << std::endl;
        for (size_t i = 0; i < std::min(points.size(), size_t(300)); ++i) {
            std::cout << "rrr  [" << i + 1 << "] hash: 0x" << std::hex << points[i].first << std::dec 
                      << ", timestamp: " << points[i].second << std::endl;
        }
    }

    std::cout << "预处理所有目标签名完成"
              << " (signature数量: " << signatures.size() << ")"
              << " (唯一哈希值数量: " << hash2TargetSignaturesInfoMap_.size() << ")" << std::endl;
}

SignatureMatcher::~SignatureMatcher() = default;



void SignatureMatcher::processQuerySignature(
    const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount) {
    
    // 计算两个时间戳之间的近似时间偏移, 对于相近的时间戳，统一收敛到一个值，这样能提高系统的鲁棒性
    // TODO: 确认用int32_t存储时，时间的单位是什么
    auto approximate_time_offset_func = [this](double queryTime, double targetTime) -> int32_t {
        double offset_ms = (targetTime - queryTime) * 1000;
        // 量化到offsetTolerance_的倍数
        int32_t quantized_offset = static_cast<int32_t>(std::round(offset_ms / offsetTolerance_ * 1000)) * offsetTolerance_ * 1000;
        // 确保返回非负值
        return quantized_offset;
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
    
    int queryPointprint = 0;

    // step1 add/update candidate
    for (const auto& queryPoint : querySignature) {
        ++queryPointprint;

        auto targetIt = hash2TargetSignaturesInfoMap_.find(queryPoint.hash);
        if (targetIt == hash2TargetSignaturesInfoMap_.end()) {
            continue;
        }

        const auto& targetSignaturesInfoList = targetIt->second;
        for (const auto& targetSignaturesInfo : targetSignaturesInfoList) {
            const auto offset = approximate_time_offset_func(queryPoint.timestamp, targetSignaturesInfo.hashTimestamp);

            // if session2CandidateMap_ not contains the key, or query time is less than candidate.lastMatchTime, then create a new candidate session
            // if session2CandidateMap_ contains the key, and query time is greater than candidate.lastMatchTime, then update the candidate session

            const auto sessionKey = CandidateSessionKey{
                .offset = offset,
                .signature = targetSignaturesInfo.signature
            };

            // 输出前100个querypoint的hash、timestamp、targetSignaturesInfo.hashTimestamp、offset、sessionKey的hash值
            const auto foundCandidate = session2CandidateMap_.find(sessionKey) != session2CandidateMap_.end();
            if (queryPointprint < 300) {
                std::cout << "rrr queryPointprint: " << queryPointprint << " hash: 0x" << std::hex << queryPoint.hash << std::dec
                          << ", timestamp: " << queryPoint.timestamp 
                          << ", targetSignaturesInfo.hashTimestamp: " << targetSignaturesInfo.hashTimestamp 
                          << ", offset: " << offset 
                          << ", sessionKey: " << hash_seesion_key_func(sessionKey) 
                          << ", foundCandidate: " << foundCandidate 
                          << ", lastMatchTime: " << (foundCandidate ? session2CandidateMap_[sessionKey].lastMatchTime : 0) << std::endl;
            }



            if (session2CandidateMap_.find(sessionKey) != session2CandidateMap_.end()) {
                auto& candidate = session2CandidateMap_[sessionKey];
                if (candidate.isNotified) {
                    continue;
                }
                if (queryPoint.timestamp >= candidate.lastMatchTime) {
                    candidate.matchCount += 1;
                    candidate.matchInfos.push_back(DebugMatchInfo { hexHashString(queryPoint.hash), queryPoint.timestamp, targetSignaturesInfo.hashTimestamp, offset });
                    candidate.lastMatchTime = queryPoint.timestamp;
                    candidate.isMatchCountChanged = true;

                    if (queryPointprint < 300) {
                        std::cout << "rrr add matchcount: " << queryPointprint << " hash: 0x" << std::hex << queryPoint.hash << std::dec
                          << ", timestamp: " << queryPoint.timestamp 
                          << ", targetSignaturesInfo.hashTimestamp: " << targetSignaturesInfo.hashTimestamp 
                          << ", offset: " << offset 
                          << ", sessionKey: " << hash_seesion_key_func(sessionKey) 
                          << ", foundCandidate: " << foundCandidate
                          << ", matchcount: " << candidate.matchCount << std::endl;
            }

                    continue;
                } 
            }
            
//            if (signature2SessionCnt_[targetSignaturesInfo.signature] > 3) {  // max 3
//                continue;
//            }

            if (session2CandidateMap_.size() < maxCandidates_) {
                signature2SessionCnt_[targetSignaturesInfo.signature] += 1;
                
                double channelRatio = 1.0;
                const auto targetChannelCount = targetSignaturesInfo.mediaItem->channelCount();
                if (targetChannelCount > 0) {
                    // 如果候选音频通道数大于输入音频通道数，则根据通道比例调整最大可匹配特征数
                    channelRatio = std::min(1.0, static_cast<double>(inputChannelCount) / targetChannelCount);
                }
                // 计算考虑通道比例后的最大可匹配特征数
                const auto targetHashesCount = targetSignaturesInfo.signature->size();
                const auto maxPossibleMatches = static_cast<size_t>(targetHashesCount * channelRatio);

                MatchingCandidate candidate = {
                    .targetSignatureInfo = &targetSignaturesInfo,
                    .maxPossibleMatches = maxPossibleMatches,
                    .matchCount = 1,
                    .matchInfos = { DebugMatchInfo { hexHashString(queryPoint.hash), queryPoint.timestamp, targetSignaturesInfo.hashTimestamp } },
                    .lastMatchTime = queryPoint.timestamp,
                    .offset = offset,
                    .isMatchCountChanged = true,
                    .isNotified = false,
                };

                if (session2CandidateMap_.find(sessionKey) != session2CandidateMap_.end()) {
                    int i = 0;
                }

                session2CandidateMap_[sessionKey] = candidate;  // TODO: 这里会覆盖原来的记录，需要重新算一个独立的key
                                    if (queryPointprint < 300) {
                        std::cout << "rrr add new candidate: " << queryPointprint << " hash: 0x" << std::hex << queryPoint.hash << std::dec 
                          << ", timestamp: " << queryPoint.timestamp 
                          << ", targetSignaturesInfo.hashTimestamp: " << targetSignaturesInfo.hashTimestamp 
                          << ", offset: " << offset 
                          << ", sessionKey: " << hash_seesion_key_func(sessionKey) 
                          << ", matchcount: " << candidate.matchCount << std::endl;
            }
            }
        }
    }


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
            std::cout << "  [" << i + 1 << "] MediaItem: " 
                      << candidate.targetSignatureInfo->mediaItem->title()
                      << ", Offset: " << key.offset
                      << ", MatchCount: " << candidate.matchCount
                      << ", MaxPossible: " << candidate.maxPossibleMatches
                      << ", Confidence: " << evaluateConfidenceFunc(candidate)
                      << ", LastMatchTime: " << candidate.lastMatchTime
                      << ", sessionKey: " << hash_seesion_key_func(key)
                      << std::endl;
        }
        auto duplicateMatchInfosOfCandidates = findDuplicateHashes(candidates);
        int i = 0;
    }

    


    for (const auto& [sessionKey, candidate] : session2CandidateMap_) {

        if (candidate.isMatchCountChanged && !candidate.isNotified) {
            const auto confidence = evaluateConfidenceFunc(candidate);
            if (confidence >= minConfidenceThreshold_ &&  candidate.matchCount >= minMatchesRequired_) {
                auto matchResult = MatchResult{
                    .mediaItem = candidate.targetSignatureInfo->mediaItem,
                    .offset = double(candidate.offset),  // TODO: candidate.offset is ms but offset is sec
                    .confidence = confidence,
                    .matchedPoints = {},
                    .id = 0,
                };
                matchResults_.push_back(matchResult);
                session2CandidateMap_[sessionKey].isNotified = true;
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
    
    // // 执行匹配
    // performMatching(querySignature, inputChannelCount);
    
    // // 更新候选结果状态

    // updateCandidates(currentTimestamp);
}

// void SignatureMatcher::processQuerySignature(
//     const std::vector<SignaturePoint>& querySignature, size_t inputChannelCount) {
    
//     if (querySignature.empty()) {
//         return;
//     }
    
//     double currentTimestamp = querySignature.back().timestamp;
    
//     // 执行匹配
//     performMatching(querySignature, inputChannelCount);
    
//     // 更新候选结果状态
//     updateCandidates(currentTimestamp);
// }

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
                candidate.mediaItem,
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
