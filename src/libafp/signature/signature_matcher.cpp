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
    
    // Reset visualization data if collection is enabled
    if (collectVisualizationData_) {
        visualizationData_ = VisualizationData();
        visualizationData_.title = "Query Audio";
        
        // Add all query points to visualization data
        for (const auto& point : querySignature) {
            visualizationData_.fingerprintPoints.emplace_back(
                point.frequency, point.timestamp, point.hash);
            
            // Also add as general peak for better visualization
            visualizationData_.allPeaks.emplace_back(point.frequency, point.timestamp);
        }
        
        // Set duration to the last timestamp + buffer
        if (!querySignature.empty()) {
            visualizationData_.duration = querySignature.back().timestamp + 1.0;
        }
    }
    
    int queryPointprint = 0;
    int queryPointHitCount = 0;

    // step1 add/update candidate
    for (const auto& queryPoint : querySignature) {
        ++queryPointprint;

        auto targetIt = hash2TargetSignaturesInfoMap_.find(queryPoint.hash);
        if (targetIt == hash2TargetSignaturesInfoMap_.end()) {
            continue;
        }
        ++queryPointHitCount;

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

                    // Add to visualization data if enabled
                    if (collectVisualizationData_) {
                        visualizationData_.matchedPoints.emplace_back(
                            queryPoint.frequency, queryPoint.timestamp, queryPoint.hash, sessionKey.offset);
                    }

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

                // Add to visualization data if enabled
                if (collectVisualizationData_) {
                    visualizationData_.matchedPoints.emplace_back(
                        queryPoint.frequency, queryPoint.timestamp, queryPoint.hash, sessionKey.offset);
                }

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
    std::cout << "rrr queryPointHitCount: " << queryPointHitCount << std::endl;

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
            // confidence >= minConfidenceThreshold_ &&  
            if (candidate.matchCount >= minMatchesRequired_) {
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
}
// Add after the constructor implementations

// Save visualization data to file
bool SignatureMatcher::saveVisualization(const std::string& filename) const {
    if (!collectVisualizationData_) {
        std::cerr << "Visualization data collection is not enabled" << std::endl;
        return false;
    }
    
    // Save data to JSON (no Python script generation)
    return Visualizer::saveVisualization(visualizationData_, filename);
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
    size_t sessionsToInclude = std::min(candidates.size(), size_t(3));
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
    
    // Create a map of session IDs to each matched point in query data
    std::unordered_map<uint32_t, uint32_t> hashToSessionMap;
    
    // Extract top sessions for visualizing
    std::vector<std::pair<CandidateSessionKey, MatchingCandidate>> candidates;
    for (const auto& pair : session2CandidateMap_) {
        candidates.push_back(pair);
    }
    
    // Sort by match count in descending order
    std::sort(candidates.begin(), candidates.end(), 
        [](const auto& a, const auto& b) {
            return a.second.matchCount > b.second.matchCount;
        });
    
    // Take top 3 sessions
    std::vector<SessionData> topSessions;
    size_t sessionsToInclude = std::min(candidates.size(), size_t(3));
    
    // Collect all matched points from query data, assigning session IDs
    VisualizationData sessionQueryData = visualizationData_;
    sessionQueryData.matchedPoints.clear(); // Clear existing matched points
    
    // For each top session, assign IDs to the matched points
    for (size_t i = 0; i < sessionsToInclude; ++i) {
        const auto& [key, candidate] = candidates[i];
        uint32_t sessionId = i + 1; // Use 1-based index for session ID
        
        // Add session to top sessions list
        SessionData sessionData;
        sessionData.id = sessionId;
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
        sessionData.mediaTitle = candidate.targetSignatureInfo->mediaItem->title();
        topSessions.push_back(sessionData);
        
        // Create a map of hashes to matched fingerprint points in source data
        std::unordered_map<uint32_t, std::vector<size_t>> sourceHashToIndexMap;
        for (size_t idx = 0; idx < sourceData.fingerprintPoints.size(); ++idx) {
            uint32_t hash = std::get<2>(sourceData.fingerprintPoints[idx]);
            sourceHashToIndexMap[hash].push_back(idx);
        }
        
        // For each match info in this candidate
        for (const auto& matchInfo : candidate.matchInfos) {
            // Extract the hash from the string format (e.g., " 0x12345")
            std::string hashStr = matchInfo.hash;
            uint32_t hash = 0;
            
            // Parse the hash value from string format
            if (hashStr.find("0x") != std::string::npos) {
                std::stringstream ss;
                ss << std::hex << hashStr.substr(hashStr.find("0x") + 2);
                ss >> hash;
            }
            
            // Add to hashToSessionMap
            hashToSessionMap[hash] = sessionId;
            
            // Add this point to the query data with session ID
            for (const auto& point : visualizationData_.matchedPoints) {
                if (std::get<2>(point) == hash) {
                    // Create a new point with session ID
                    sessionQueryData.matchedPoints.push_back(
                        std::make_tuple(std::get<0>(point), std::get<1>(point), std::get<2>(point), sessionId)
                    );
                }
            }
            
            // Find and add corresponding points in source data
            auto it = sourceHashToIndexMap.find(hash);
            if (it != sourceHashToIndexMap.end()) {
                for (size_t idx : it->second) {
                    const auto& sourcePoint = sourceData.fingerprintPoints[idx];
                    enhancedSourceData.matchedPoints.push_back(
                        std::make_tuple(std::get<0>(sourcePoint), std::get<1>(sourcePoint), std::get<2>(sourcePoint), sessionId)
                    );
                }
            }
        }
    }
    
    // Save source data, query data, and sessions data
    bool success = Visualizer::saveVisualization(enhancedSourceData, sourceFilename) &&
                   Visualizer::saveVisualization(sessionQueryData, queryFilename) &&
                   Visualizer::saveSessionsData(topSessions, sessionsFilename);
    
    return success;
}

} // namespace afp 
