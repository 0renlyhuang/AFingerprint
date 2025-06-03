#pragma once
#include <memory>
#include "afp/iperformance_config.h"

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
    
    // 创建生成模式的配置 (优先精度)
    static std::shared_ptr<IPerformanceConfig> createMobileGenConfig();
    static std::shared_ptr<IPerformanceConfig> createDesktopGenConfig();
    static std::shared_ptr<IPerformanceConfig> createServerGenConfig();
};

} // namespace afp 