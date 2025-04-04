#include "matcher.h"
#include "audio_debugger.h"
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <iomanip>
#include <unordered_set>

namespace afp {

Matcher::Matcher(const Catalog& catalog)
    : catalog_(catalog) {
    generator_ = std::make_unique<SignatureGenerator>();
    generator_->init();
}

Matcher::~Matcher() = default;

bool Matcher::appendStreamBuffer(const float* buffer, 
                              size_t bufferSize,
                              double startTimestamp) {
    if (!generator_->appendStreamBuffer(buffer, bufferSize, startTimestamp)) {
        return false;
    }

    // 打印匹配过程中生成的查询指纹
    const auto& querySignature = generator_->signature();
    std::cout << "匹配过程中生成的查询指纹:" << std::endl;
    AudioDebugger::printSignatureDetails(querySignature);

    performMatching();
    return true;
}

void Matcher::performMatching() {
    const auto& querySignature = generator_->signature();
    if (querySignature.empty()) {
        return;
    }

    bool anyMatchFound = false;
    std::cout << "开始匹配过程，查询指纹点数量: " << querySignature.size() << std::endl;
    AudioDebugger::printQuerySignatureStats(querySignature);

    // 计算指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueQueryHashes;
    for (const auto& point : querySignature) {
        uniqueQueryHashes.insert(point.hash);
    }

    for (size_t i = 0; i < catalog_.signatures().size(); ++i) {
        const auto& targetSignature = catalog_.signatures()[i];
        const auto& mediaItem = catalog_.mediaItems()[i];

        // 使用AudioDebugger打印目标指纹统计信息
        AudioDebugger::printTargetSignatureStats(targetSignature, mediaItem.title(), i);
        
        // 检查数据库指纹是否完整
        if (targetSignature.empty()) {
            std::cerr << "警告: 数据库中的指纹 #" << i << " (" << mediaItem.title() << ") 是空的!" << std::endl;
            continue;
        }

        // 计算目标指纹中的唯一哈希值
        std::unordered_set<uint32_t> uniqueTargetHashes;
        for (const auto& point : targetSignature) {
            uniqueTargetHashes.insert(point.hash);
        }

        // 使用AudioDebugger打印哈希交集信息
        AudioDebugger::printCommonHashesInfo(uniqueQueryHashes, uniqueTargetHashes);
        
        // 比较哈希值
        size_t hashMatches = 0;
        for (const auto& queryPoint : querySignature) {
            for (const auto& targetPoint : targetSignature) {
                if (queryPoint.hash == targetPoint.hash) {
                    hashMatches++;
                    // 只打印前几个匹配的详细信息
                    if (hashMatches <= 100) {
                        std::cout << "  哈希匹配: 0x" << std::hex << queryPoint.hash << std::dec 
                                << " (查询时间: " << queryPoint.timestamp 
                                << "s, 目标时间: " << targetPoint.timestamp << "s)" << std::endl;
                    }
                    break; // 找到一个匹配就跳出内循环
                }
            }
        }
        std::cout << "  哈希值匹配数量: " << hashMatches << std::endl;

        double offset;
        double confidence = computeSimilarity(querySignature, targetSignature, offset);
        std::cout << "  计算相似度结果: 置信度=" << confidence << ", 偏移=" << offset << std::endl;

        if (confidence >= 0.25 && hashMatches >= 30) {
            anyMatchFound = true;
            MatchResult result{
                mediaItem,
                offset,
                confidence,
                querySignature  // 这里可以优化，只返回匹配的点
            };

            if (matchCallback_) {
                matchCallback_(result);
            }
        } else {
            std::cout << "  匹配置信度不足 (阈值: 0.25)" << std::endl;
        }
    }

    if (!anyMatchFound) {
        std::cout << "未找到任何匹配结果" << std::endl;
    }
}

double Matcher::computeSimilarity(const std::vector<SignaturePoint>& query,
                                const std::vector<SignaturePoint>& target,
                                double& offset) {
    if (query.empty() || target.empty()) {
        return 0.0;
    }

    // 使用哈希表加速查找
    std::unordered_map<uint32_t, std::vector<size_t>> targetHashIndex;
    for (size_t i = 0; i < target.size(); ++i) {
        targetHashIndex[target[i].hash].push_back(i);
    }

    // 统计时间偏移
    std::unordered_map<double, size_t> offsetCounts;
    size_t totalMatches = 0;

    for (const auto& queryPoint : query) {
        auto it = targetHashIndex.find(queryPoint.hash);
        if (it != targetHashIndex.end()) {
            for (size_t targetIdx : it->second) {
                double timeOffset = target[targetIdx].timestamp - queryPoint.timestamp;
                offsetCounts[timeOffset]++;
                totalMatches++;
            }
        }
    }

    if (totalMatches < kMinMatches) {
        return 0.0;
    }

    // 找到最常见的偏移
    size_t maxCount = 0;
    double bestOffset = 0.0;
    for (const auto& [offset, count] : offsetCounts) {
        if (count > maxCount) {
            maxCount = count;
            bestOffset = offset;
        }
    }

    offset = bestOffset;
    // 计算置信度时考虑到查询和目标的相对大小
    double confidence = static_cast<double>(maxCount) / std::min(query.size(), target.size());
    
    // 使用AudioDebugger打印相似度计算的详细信息
    AudioDebugger::printSimilarityDebugInfo(totalMatches, bestOffset, maxCount, confidence, 
                                         query.size(), target.size());
    
    return confidence;
}

} // namespace afp 