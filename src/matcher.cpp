#include "matcher.h"
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
    std::cout << "  - 指纹点数量: " << querySignature.size() << std::endl;
    
    if (!querySignature.empty()) {
        std::cout << "  - 前10个指纹点:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(10), querySignature.size()); ++i) {
            std::cout << "    [" << i << "] Hash: 0x" 
                     << std::hex << std::setw(8) << std::setfill('0') << querySignature[i].hash
                     << std::dec << ", Timestamp: " << querySignature[i].timestamp << std::endl;
        }
    }
    std::cout << std::endl;

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

    // 调试输出：计算指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueQueryHashes;
    for (const auto& point : querySignature) {
        uniqueQueryHashes.insert(point.hash);
    }
    std::cout << "查询指纹中唯一哈希值数量: " << uniqueQueryHashes.size() << std::endl;

    for (size_t i = 0; i < catalog_.signatures().size(); ++i) {
        const auto& targetSignature = catalog_.signatures()[i];
        const auto& mediaItem = catalog_.mediaItems()[i];

        // 调试输出：计算目标指纹中的唯一哈希值
        std::unordered_set<uint32_t> uniqueTargetHashes;
        for (const auto& point : targetSignature) {
            uniqueTargetHashes.insert(point.hash);
        }

        std::cout << "比较与 '" << mediaItem.title() << "' 的指纹 (目标指纹点数量: " << targetSignature.size() 
                  << ", 唯一哈希值: " << uniqueTargetHashes.size() << ")" << std::endl;
        
        // 检查数据库指纹是否完整
        if (targetSignature.empty()) {
            std::cerr << "警告: 数据库中的指纹 #" << i << " (" << mediaItem.title() << ") 是空的!" << std::endl;
            continue;
        }

        // 检查指纹哈希是否有交集
        std::unordered_set<uint32_t> commonHashes;
        for (const auto& hash : uniqueQueryHashes) {
            if (uniqueTargetHashes.count(hash) > 0) {
                commonHashes.insert(hash);
            }
        }
        std::cout << "  共同哈希值数量: " << commonHashes.size() << std::endl;
        
        // 如果有共同哈希，输出它们
        if (!commonHashes.empty() && commonHashes.size() <= 10) {
            std::cout << "  共同哈希值: ";
            for (const auto& hash : commonHashes) {
                std::cout << "0x" << std::hex << hash << std::dec << " ";
            }
            std::cout << std::endl;
        }
        
        // 比较哈希值
        size_t hashMatches = 0;
        for (const auto& queryPoint : querySignature) {
            for (const auto& targetPoint : targetSignature) {
                if (queryPoint.hash == targetPoint.hash) {
                    hashMatches++;
                    // 只打印前几个匹配的详细信息
                    if (hashMatches <= 5) {
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
    
    // 调试输出
    std::cout << "Debug: Total matches: " << totalMatches
              << ", Best offset: " << bestOffset
              << ", Max count: " << maxCount
              << ", Confidence: " << confidence
              << ", Query size: " << query.size()
              << ", Target size: " << target.size() << std::endl;
    
    return confidence;
}

} // namespace afp 