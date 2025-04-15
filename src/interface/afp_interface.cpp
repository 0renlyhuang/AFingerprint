#include "interface/afp_interface.h"
#include "catalog.h"
#include "signature/signature_generator.h"
#include "config/performance_config_factory.h"
#include "matcher.h"

namespace afp {

namespace interface {

std::shared_ptr<ICatalog> createCatalog() {
    return std::make_shared<Catalog>();
}

std::shared_ptr<ISignatureGenerator> createSignatureGenerator(
    std::shared_ptr<IPerformanceConfig> config) {
    return std::make_shared<SignatureGenerator>(config);
}

std::shared_ptr<IPerformanceConfig> createPerformanceConfig(
    PlatformType platform) {
    return PerformanceConfigFactory::getConfig(platform);
}

std::shared_ptr<IMatcher> createMatcher(
    std::shared_ptr<ICatalog> catalog,
    std::shared_ptr<IPerformanceConfig> config,
    const PCMFormat& format) {
    return std::make_shared<Matcher>(catalog, config, format);
}

} // namespace interface

} // namespace afp 