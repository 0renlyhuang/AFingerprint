#pragma once

#include <memory>
#include "afp/icatalog.h"
#include "afp/isignature_generator.h"
#include "afp/imatcher.h"
#include "afp/iperformance_config.h"

namespace afp::interface {


// 创建Catalog对象
std::shared_ptr<ICatalog> createCatalog();

// 创建SignatureGenerator对象
std::shared_ptr<ISignatureGenerator> createSignatureGenerator(
    std::shared_ptr<IPerformanceConfig> config);

// 创建PerformanceConfig对象
std::shared_ptr<IPerformanceConfig> createPerformanceConfig(
    PlatformType platform);

// 创建Matcher对象
std::shared_ptr<IMatcher> createMatcher(
    std::shared_ptr<ICatalog> catalog,
    std::shared_ptr<IPerformanceConfig> config,
    const PCMFormat& format);

} // namespace afp::interface 