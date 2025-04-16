#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "signature/signature_generator.h"
#include "afp/media_item.h"
#include "afp/icatalog.h"

namespace afp {

class Catalog : public ICatalog {
public:
    Catalog() = default;
    ~Catalog() override = default;

    // 添加指纹和媒体信息
    void addSignature(const std::vector<SignaturePoint>& signature, 
                     const MediaItem& mediaItem) override;

    // 序列化到文件
    bool saveToFile(const std::string& filename) const override;

    // 从文件反序列化
    bool loadFromFile(const std::string& filename) override;

    // 获取所有指纹
    const std::vector<std::vector<SignaturePoint>>& signatures() const override {
        return signatures_;
    }

    // 获取所有媒体信息
    const std::vector<MediaItem>& mediaItems() const override {
        return mediaItems_;
    }

private:
    // 文件格式版本
    static constexpr uint32_t kFileVersion = 1;

    // 文件头部结构
    struct FileHeader {
        uint32_t version;
        uint32_t numEntries;
    };

    // 序列化辅助函数
    bool writeHeader(std::ofstream& file) const;
    bool writeEntry(std::ofstream& file, 
                   const std::vector<SignaturePoint>& signature,
                   const MediaItem& mediaItem) const;
    bool readHeader(std::ifstream& file, FileHeader& header) const;
    bool readEntry(std::ifstream& file,
                  std::vector<SignaturePoint>& signature,
                  MediaItem& mediaItem);

private:
    std::vector<std::vector<SignaturePoint>> signatures_;
    std::vector<MediaItem> mediaItems_;
};

} // namespace afp 