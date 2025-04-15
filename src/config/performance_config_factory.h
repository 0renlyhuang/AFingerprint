#pragma once
#include <memory>
#include "iperformance_config.h"

namespace afp {

class PerformanceConfigFactory {
public:
    // 禁用构造函数和析构函数
    PerformanceConfigFactory() = delete;
    ~PerformanceConfigFactory() = delete;

    // 获取指定平台的配置
    static std::shared_ptr<IPerformanceConfig> getConfig(PlatformType platform);

private:
    // 创建不同平台的配置
    static std::shared_ptr<IPerformanceConfig> createMobileConfig();
    static std::shared_ptr<IPerformanceConfig> createDesktopConfig();
    static std::shared_ptr<IPerformanceConfig> createServerConfig();
};

} // namespace afp 