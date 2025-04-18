#include "matcher.h"
#include "debugger/audio_debugger.h"
#include <iostream>

namespace afp {

Matcher::Matcher(std::shared_ptr<ICatalog> catalog, std::shared_ptr<IPerformanceConfig> config, const PCMFormat& format)
    : catalog_(catalog)
    , format_(format) {
    generator_ = std::make_unique<SignatureGenerator>(config);
    generator_->init(format);
    
    // 将目录传递给SignatureMatcher，让它预处理目标签名
    signatureMatcher_ = std::make_unique<SignatureMatcher>(catalog, config);
}

Matcher::~Matcher() = default;

bool Matcher::appendStreamBuffer(const void* buffer, 
                              size_t bufferSize,
                              double startTimestamp) {
    if (!generator_->appendStreamBuffer(buffer, bufferSize, startTimestamp)) {
        return false;
    }

    // 获取生成的查询指纹
    const auto& querySignature = generator_->signature();

    std::unordered_set<uint32_t> unique_hash_set;
    for (const auto& point : querySignature) {
        unique_hash_set.insert(point.hash);
    }

    std::unordered_set<std::string> unique_hash_timestamp_set;
    for (const auto& point : querySignature) {
        unique_hash_timestamp_set.insert(std::to_string(point.hash) + "_" + std::to_string(point.timestamp));
    }
    
    // 仅在调试模式下打印详情
    std::cout << "生成查询指纹点数: " << querySignature.size() << std::endl;
    std::cout << "唯一哈希值数量: " << unique_hash_set.size() << std::endl;
    std::cout << "唯一哈希值+时间戳数量: " << unique_hash_timestamp_set.size() << std::endl;
    AudioDebugger::printSignatureDetails(querySignature);


    // 按所有point的时间排序，输出前100个point的hash和timestamp
    std::vector<std::pair<uint32_t, double>> points;
    for (const auto& point : querySignature) {
        points.emplace_back(point.hash, point.timestamp);
    }
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    std::cout << "rrr query: 按所有point的时间排序，输出前100个point的hash和timestamp" << std::endl;
    for (size_t i = 0; i < std::min(points.size(), size_t(300)); ++i) {
        std::cout << "rrr  [" << i + 1 << "] hash: 0x" << std::hex << points[i].first << std::dec 
                  << ", timestamp: " << points[i].second << std::endl;
    }

    // 将查询指纹传递给SignatureMatcher处理，并传入通道数量
    signatureMatcher_->processQuerySignature(querySignature, format_.channels());
    
    return true;
}

} // namespace afp 