#pragma once
#include <string>
#include <vector>
#include "afp/isignature_generator.h"
#include "afp/media_item.h"

namespace afp {

class ICatalog {
public:
    virtual ~ICatalog() = default;

    // 添加指纹和媒体信息
    virtual void addSignature(const std::vector<SignaturePoint>& signature, 
                            const MediaItem& mediaItem) = 0;

    // 序列化到文件
    virtual bool saveToFile(const std::string& filename) const = 0;

    // 从文件反序列化
    virtual bool loadFromFile(const std::string& filename) = 0;

    // 获取所有指纹
    virtual const std::vector<std::vector<SignaturePoint>>& signatures() const = 0;

    // 获取所有媒体信息
    virtual const std::vector<MediaItem>& mediaItems() const = 0;
};

} // namespace afp 