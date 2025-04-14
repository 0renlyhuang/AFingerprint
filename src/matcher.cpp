#include "matcher.h"
#include "debugger/audio_debugger.h"
#include <iostream>

namespace afp {

Matcher::Matcher(const Catalog& catalog, std::shared_ptr<PerformanceConfig> config, const PCMFormat& format)
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
    
    // 仅在调试模式下打印详情
    std::cout << "生成查询指纹点数: " << querySignature.size() << std::endl;
    AudioDebugger::printSignatureDetails(querySignature);

    // 将查询指纹传递给SignatureMatcher处理，并传入通道数量
    signatureMatcher_->processQuerySignature(querySignature, format_.channels());
    
    return true;
}

} // namespace afp 