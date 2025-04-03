#pragma once
#include <string>
#include <map>

namespace afp {

class MediaItem {
public:
    MediaItem() = default;
    ~MediaItem() = default;

    // 设置基本信息
    void setTitle(const std::string& title) { title_ = title; }
    void setSubtitle(const std::string& subtitle) { subtitle_ = subtitle; }
    
    // 设置自定义信息
    void setCustomInfo(const std::string& key, const std::string& value) {
        customInfo_[key] = value;
    }

    // 获取信息
    const std::string& title() const { return title_; }
    const std::string& subtitle() const { return subtitle_; }
    const std::map<std::string, std::string>& customInfo() const { return customInfo_; }

private:
    std::string title_;
    std::string subtitle_;
    std::map<std::string, std::string> customInfo_;
};

} // namespace afp 