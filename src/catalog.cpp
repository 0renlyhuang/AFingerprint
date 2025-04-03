#include "catalog.h"
#include <fstream>
#include <cstring>
#include <iostream>

namespace afp {

void Catalog::addSignature(const std::vector<SignaturePoint>& signature, 
                         const MediaItem& mediaItem) {
    signatures_.push_back(signature);
    mediaItems_.push_back(mediaItem);
}

bool Catalog::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "无法打开文件进行写入: " << filename << std::endl;
        return false;
    }

    std::cout << "正在保存 " << signatures_.size() << " 个指纹到数据库..." << std::endl;

    // 写入文件头
    FileHeader header;
    header.version = kFileVersion;
    header.numEntries = static_cast<uint32_t>(signatures_.size());
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!file.good()) {
        std::cerr << "写入文件头失败" << std::endl;
        return false;
    }

    // 打印文件头信息
    std::cout << "写入文件头: 版本=" << header.version 
              << ", 条目数量=" << header.numEntries 
              << ", 大小=" << sizeof(header) << "字节" << std::endl;

    // 写入所有条目
    for (size_t i = 0; i < signatures_.size(); ++i) {
        std::cout << "保存指纹 #" << i << " (" << mediaItems_[i].title() 
                  << "), 指纹点数量: " << signatures_[i].size() << std::endl;
                  
        // 打印前几个哈希值用于调试
        if (!signatures_[i].empty()) {
            std::cout << "  前5个哈希值: ";
            for (size_t j = 0; j < std::min(size_t(5), signatures_[i].size()); ++j) {
                std::cout << "0x" << std::hex << signatures_[i][j].hash << std::dec << " ";
            }
            std::cout << std::endl;
        }
        
        // 检查指纹是否为空
        if (signatures_[i].empty()) {
            std::cerr << "警告: 指纹 #" << i << " (" << mediaItems_[i].title() << ") 是空的" << std::endl;
        }
        
        // 写入签名点数量
        uint32_t numPoints = static_cast<uint32_t>(signatures_[i].size());
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));
        if (!file.good()) {
            std::cerr << "写入指纹点数量失败" << std::endl;
            return false;
        }

        // 写入签名点数据
        if (numPoints > 0) {
            size_t dataSize = numPoints * sizeof(SignaturePoint);
            file.write(reinterpret_cast<const char*>(signatures_[i].data()), dataSize);
            if (!file.good()) {
                std::cerr << "写入指纹点数据失败 (尝试写入 " << dataSize << " 字节)" << std::endl;
                return false;
            }
        }

        // 写入媒体信息
        const std::string& title = mediaItems_[i].title();
        const std::string& subtitle = mediaItems_[i].subtitle();
        const auto& customInfo = mediaItems_[i].customInfo();

        // 写入标题长度和数据
        uint32_t titleLen = static_cast<uint32_t>(title.length());
        file.write(reinterpret_cast<const char*>(&titleLen), sizeof(titleLen));
        if (!file.good()) {
            std::cerr << "写入标题长度失败" << std::endl;
            return false;
        }
        
        if (titleLen > 0) {
            file.write(title.c_str(), titleLen);
            if (!file.good()) {
                std::cerr << "写入标题失败" << std::endl;
                return false;
            }
        }

        // 写入副标题长度和数据
        uint32_t subtitleLen = static_cast<uint32_t>(subtitle.length());
        file.write(reinterpret_cast<const char*>(&subtitleLen), sizeof(subtitleLen));
        if (!file.good()) {
            std::cerr << "写入副标题长度失败" << std::endl;
            return false;
        }
        
        if (subtitleLen > 0) {
            file.write(subtitle.c_str(), subtitleLen);
            if (!file.good()) {
                std::cerr << "写入副标题失败" << std::endl;
                return false;
            }
        }

        // 写入自定义信息数量
        uint32_t numCustomInfo = static_cast<uint32_t>(customInfo.size());
        file.write(reinterpret_cast<const char*>(&numCustomInfo), sizeof(numCustomInfo));
        if (!file.good()) {
            std::cerr << "写入自定义信息数量失败" << std::endl;
            return false;
        }

        // 写入自定义信息
        for (const auto& [key, value] : customInfo) {
            uint32_t keyLen = static_cast<uint32_t>(key.length());
            uint32_t valueLen = static_cast<uint32_t>(value.length());

            file.write(reinterpret_cast<const char*>(&keyLen), sizeof(keyLen));
            if (!file.good()) {
                std::cerr << "写入自定义信息键长度失败" << std::endl;
                return false;
            }
            
            if (keyLen > 0) {
                file.write(key.c_str(), keyLen);
                if (!file.good()) {
                    std::cerr << "写入自定义信息键失败" << std::endl;
                    return false;
                }
            }

            file.write(reinterpret_cast<const char*>(&valueLen), sizeof(valueLen));
            if (!file.good()) {
                std::cerr << "写入自定义信息值长度失败" << std::endl;
                return false;
            }
            
            if (valueLen > 0) {
                file.write(value.c_str(), valueLen);
                if (!file.good()) {
                    std::cerr << "写入自定义信息值失败" << std::endl;
                    return false;
                }
            }
        }
    }

    // 添加校验和
    uint32_t checksum = static_cast<uint32_t>(signatures_.size());
    file.write(reinterpret_cast<const char*>(&checksum), sizeof(checksum));
    if (!file.good()) {
        std::cerr << "写入校验和失败" << std::endl;
        return false;
    }
    
    // 获取文件大小
    size_t fileSize = file.tellp();
    std::cout << "保存成功，文件大小: " << fileSize << " 字节" << std::endl;

    return true;
}

bool Catalog::loadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "无法打开文件进行读取: " << filename << std::endl;
        return false;
    }

    // 检查文件大小
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::cout << "文件大小: " << fileSize << " 字节" << std::endl;
    
    if (fileSize < sizeof(FileHeader)) {
        std::cerr << "错误: 文件太小，无法包含有效的头部 (需要至少 " 
                 << sizeof(FileHeader) << " 字节)" << std::endl;
        return false;
    }

    // 读取文件头
    FileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good()) {
        std::cerr << "读取文件头失败" << std::endl;
        return false;
    }

    // 检查版本
    if (header.version != kFileVersion) {
        std::cerr << "文件版本不匹配: 期望 " << kFileVersion << ", 实际 " << header.version << std::endl;
        return false;
    }

    // 检查条目数量是否合理
    if (header.numEntries > 1000) {  // 设置一个合理的上限
        std::cerr << "警告: 条目数量异常大 (" << header.numEntries 
                 << ")，可能是文件损坏或格式错误" << std::endl;
        return false;
    }

    std::cout << "读取指纹数据库，条目数量: " << header.numEntries << std::endl;

    // 清空现有数据
    signatures_.clear();
    mediaItems_.clear();

    // 读取所有条目
    for (uint32_t i = 0; i < header.numEntries; ++i) {
        std::vector<SignaturePoint> signature;
        MediaItem mediaItem;
        
        std::cout << "开始读取条目 #" << i << std::endl;
        if (!readEntry(file, signature, mediaItem)) {
            std::cerr << "读取条目 #" << i << " 失败" << std::endl;
            return false;
        }
        
        std::cout << "读取指纹 #" << i << " (" << mediaItem.title() 
                  << "), 指纹点数量: " << signature.size() << std::endl;
                  
        // 打印前几个哈希值用于调试
        if (!signature.empty()) {
            std::cout << "  前5个哈希值: ";
            for (size_t j = 0; j < std::min(size_t(5), signature.size()); ++j) {
                std::cout << "0x" << std::hex << signature[j].hash << std::dec << " ";
            }
            std::cout << std::endl;
        }
        
        signatures_.push_back(std::move(signature));
        mediaItems_.push_back(std::move(mediaItem));
    }

    // 检查校验和
    if (file.tellg() + static_cast<std::streampos>(sizeof(uint32_t)) <= fileSize) {
        uint32_t expectedChecksum = static_cast<uint32_t>(signatures_.size());
        uint32_t fileChecksum = 0;
        file.read(reinterpret_cast<char*>(&fileChecksum), sizeof(fileChecksum));
        
        if (!file.good()) {
            std::cerr << "警告: 读取校验和失败" << std::endl;
        } else if (fileChecksum != expectedChecksum) {
            std::cerr << "警告: 校验和不匹配，数据可能已损坏 (期望: " 
                     << expectedChecksum << ", 实际: " << fileChecksum << ")" << std::endl;
        }
    } else {
        std::cerr << "警告: 文件不包含校验和" << std::endl;
    }
    
    std::cout << "指纹数据库加载成功，总计 " << signatures_.size() << " 个指纹" << std::endl;
    return !signatures_.empty();  // 只有至少加载了一个指纹才算成功
}

bool Catalog::writeHeader(std::ofstream& file) const {
    FileHeader header;
    header.version = kFileVersion;
    header.numEntries = static_cast<uint32_t>(signatures_.size());
    
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return file.good();
}

bool Catalog::writeEntry(std::ofstream& file, 
                        const std::vector<SignaturePoint>& signature,
                        const MediaItem& mediaItem) const {
    // 写入签名点数量
    uint32_t numPoints = static_cast<uint32_t>(signature.size());
    file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));
    if (!file.good()) return false;

    // 写入签名点数据
    if (numPoints > 0) {
        file.write(reinterpret_cast<const char*>(signature.data()), 
                  numPoints * sizeof(SignaturePoint));
        if (!file.good()) return false;
    }

    // 写入媒体信息
    const std::string& title = mediaItem.title();
    const std::string& subtitle = mediaItem.subtitle();
    const auto& customInfo = mediaItem.customInfo();

    // 写入标题长度和数据
    uint32_t titleLen = static_cast<uint32_t>(title.length());
    file.write(reinterpret_cast<const char*>(&titleLen), sizeof(titleLen));
    if (!file.good()) return false;
    if (titleLen > 0) {
        file.write(title.c_str(), titleLen);
        if (!file.good()) return false;
    }

    // 写入副标题长度和数据
    uint32_t subtitleLen = static_cast<uint32_t>(subtitle.length());
    file.write(reinterpret_cast<const char*>(&subtitleLen), sizeof(subtitleLen));
    if (!file.good()) return false;
    if (subtitleLen > 0) {
        file.write(subtitle.c_str(), subtitleLen);
        if (!file.good()) return false;
    }

    // 写入自定义信息数量
    uint32_t numCustomInfo = static_cast<uint32_t>(customInfo.size());
    file.write(reinterpret_cast<const char*>(&numCustomInfo), sizeof(numCustomInfo));
    if (!file.good()) return false;

    // 写入自定义信息
    for (const auto& [key, value] : customInfo) {
        uint32_t keyLen = static_cast<uint32_t>(key.length());
        uint32_t valueLen = static_cast<uint32_t>(value.length());

        file.write(reinterpret_cast<const char*>(&keyLen), sizeof(keyLen));
        if (!file.good()) return false;
        if (keyLen > 0) {
            file.write(key.c_str(), keyLen);
            if (!file.good()) return false;
        }

        file.write(reinterpret_cast<const char*>(&valueLen), sizeof(valueLen));
        if (!file.good()) return false;
        if (valueLen > 0) {
            file.write(value.c_str(), valueLen);
            if (!file.good()) return false;
        }
    }

    return true;
}

bool Catalog::readHeader(std::ifstream& file, FileHeader& header) const {
    // 保存当前位置
    auto startPos = file.tellg();
    
    // 读取头部
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    if (!file.good()) {
        std::cerr << "错误: 读取头部失败，文件可能已损坏" << std::endl;
        return false;
    }
    
    // 检查版本
    if (header.version != kFileVersion) {
        std::cerr << "错误: 无效的文件版本 " << header.version 
                 << " (期望 " << kFileVersion << ")" << std::endl;
        return false;
    }
    
    // 检查条目数是否合理
    if (header.numEntries > 1000) {
        std::cerr << "错误: 条目数量异常大 (" << header.numEntries << ")" << std::endl;
        return false;
    }
    
    return true;
}

bool Catalog::readEntry(std::ifstream& file,
                       std::vector<SignaturePoint>& signature,
                       MediaItem& mediaItem) {
    // 读取签名点数量
    uint32_t numPoints;
    file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));
    if (!file.good()) {
        std::cerr << "错误: 读取指纹点数量失败" << std::endl;
        return false;
    }
    
    // 检查数量是否合理
    if (numPoints > 1000000) { // 设置一个合理的上限
        std::cerr << "错误: 指纹点数量异常大 (" << numPoints << ")，可能是文件损坏" << std::endl;
        return false;
    }
    
    std::cout << "  读取到指纹点数量: " << numPoints << std::endl;

    // 读取签名点数据
    signature.resize(numPoints);
    if (numPoints > 0) {
        // 计算需要读取的数据大小
        size_t dataSize = numPoints * sizeof(SignaturePoint);
        
        // 检查是否超出了文件剩余长度
        std::streampos currentPos = file.tellg();
        file.seekg(0, std::ios::end);
        std::streampos endPos = file.tellg();
        file.seekg(currentPos);
        
        if (currentPos + static_cast<std::streampos>(dataSize) > endPos) {
            std::cerr << "错误: 指纹点数据超出文件范围 (需要读取 " << dataSize 
                     << " 字节，但文件只剩 " << (endPos - currentPos) << " 字节)" << std::endl;
            return false;
        }
        
        file.read(reinterpret_cast<char*>(signature.data()), dataSize);
        if (!file.good()) {
            std::cerr << "错误: 读取指纹点数据失败 (期望读取 " << numPoints 
                     << " 个点，每个点 " << sizeof(SignaturePoint) << " 字节)" << std::endl;
            return false;
        }
    }

    // 读取标题
    uint32_t titleLen;
    file.read(reinterpret_cast<char*>(&titleLen), sizeof(titleLen));
    if (!file.good()) {
        std::cerr << "错误: 读取标题长度失败" << std::endl;
        return false;
    }
    
    // 检查标题长度是否合理
    if (titleLen > 1000) { // 设置一个合理的上限
        std::cerr << "错误: 标题长度异常大 (" << titleLen << ")，可能是文件损坏" << std::endl;
        return false;
    }
    
    std::cout << "  读取到标题长度: " << titleLen << std::endl;
    
    if (titleLen > 0) {
        std::string title(titleLen, '\0');
        file.read(&title[0], titleLen);
        if (!file.good()) {
            std::cerr << "错误: 读取标题内容失败" << std::endl;
            return false;
        }
        mediaItem.setTitle(title);
    }

    // 读取副标题
    uint32_t subtitleLen;
    file.read(reinterpret_cast<char*>(&subtitleLen), sizeof(subtitleLen));
    if (!file.good()) {
        std::cerr << "错误: 读取副标题长度失败" << std::endl;
        return false;
    }
    
    std::cout << "  读取到副标题长度: " << subtitleLen << std::endl;
    
    if (subtitleLen > 0) {
        std::string subtitle(subtitleLen, '\0');
        file.read(&subtitle[0], subtitleLen);
        if (!file.good()) {
            std::cerr << "错误: 读取副标题内容失败" << std::endl;
            return false;
        }
        mediaItem.setSubtitle(subtitle);
    }

    // 读取自定义信息数量
    uint32_t numCustomInfo;
    file.read(reinterpret_cast<char*>(&numCustomInfo), sizeof(numCustomInfo));
    if (!file.good()) {
        std::cerr << "错误: 读取自定义信息数量失败" << std::endl;
        return false;
    }
    
    std::cout << "  读取到自定义信息数量: " << numCustomInfo << std::endl;

    // 读取自定义信息
    for (uint32_t i = 0; i < numCustomInfo; ++i) {
        uint32_t keyLen;
        file.read(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
        if (!file.good()) {
            std::cerr << "错误: 读取自定义信息键长度失败" << std::endl;
            return false;
        }
        
        std::string key;
        if (keyLen > 0) {
            key.resize(keyLen);
            file.read(&key[0], keyLen);
            if (!file.good()) {
                std::cerr << "错误: 读取自定义信息键失败" << std::endl;
                return false;
            }
        }

        uint32_t valueLen;
        file.read(reinterpret_cast<char*>(&valueLen), sizeof(valueLen));
        if (!file.good()) {
            std::cerr << "错误: 读取自定义信息值长度失败" << std::endl;
            return false;
        }
        
        std::string value;
        if (valueLen > 0) {
            value.resize(valueLen);
            file.read(&value[0], valueLen);
            if (!file.good()) {
                std::cerr << "错误: 读取自定义信息值失败" << std::endl;
                return false;
            }
        }

        mediaItem.setCustomInfo(key, value);
    }

    return true;
}

} // namespace afp 