#include "debugger/audio_debugger.h"
#include "signature/signature_generator.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace afp {

void AudioDebugger::checkAudioBuffer(const float* buffer, size_t bufferSize, 
                                    double startTimestamp, bool isFirstCall) {
    // if (isFirstCall) {
    //     std::cout << "首次处理音频数据: " << bufferSize << " 样本, 起始时间戳: " 
    //               << startTimestamp << std::endl;
    // }

    // // 检查原始输入数据是否含有非零值
    // bool hasNonZeroInput = false;
    // float maxInputValue = 0.0f;
    // size_t firstNonZeroPos = 0;
    // size_t lastNonZeroPos = 0;
    
    // // 先检查前100个样本 
    // for (size_t i = 0; i < std::min(bufferSize, size_t(100)); ++i) {
    //     if (std::abs(buffer[i]) > 0.0001f) {
    //         hasNonZeroInput = true;
    //         maxInputValue = std::max(maxInputValue, std::abs(buffer[i]));
    //         if (firstNonZeroPos == 0) firstNonZeroPos = i;
    //         lastNonZeroPos = i;
    //     }
    // }
    
    // std::cout << "[Debug] 输入音频前100样本检查: 含非零值: " << (hasNonZeroInput ? "是" : "否") 
    //           << ", 前100个样本中最大值: " << maxInputValue << std::endl;
    
    // // 如果前100个样本都是0，检查整个缓冲区
    // if (!hasNonZeroInput) {
    //     // 全面扫描整个缓冲区
    //     std::cout << "[Debug] 扫描全部 " << bufferSize << " 个样本..." << std::endl;
        
    //     size_t nonZeroCount = 0;
        
    //     for (size_t i = 0; i < bufferSize; ++i) {
    //         if (std::abs(buffer[i]) > 0.0001f) {
    //             hasNonZeroInput = true;
    //             nonZeroCount++;
    //             maxInputValue = std::max(maxInputValue, std::abs(buffer[i]));
    //             if (firstNonZeroPos == 0) firstNonZeroPos = i;
    //             lastNonZeroPos = i;
                
    //             // 找到第一个非零值后，输出一些样本
    //             if (nonZeroCount == 1) {
    //                 std::cout << "[Debug] 在位置 " << i << " 找到第一个非零值: " << buffer[i] << std::endl;
    //                 std::cout << "[Debug] 样本值 " << i << " 到 " << i+9 << ": ";
    //                 for (size_t j = i; j < std::min(i+10, bufferSize); ++j) {
    //                     std::cout << buffer[j] << " ";
    //                 }
    //                 std::cout << std::endl;
    //             }
    //         }
    //     }
        
    //     std::cout << "[Debug] 全部样本扫描结果: 含非零值: " << (hasNonZeroInput ? "是" : "否") 
    //               << ", 非零值数量: " << nonZeroCount
    //               << ", 最大值: " << maxInputValue;
        
    //     if (hasNonZeroInput) {
    //         std::cout << ", 首个非零值位置: " << firstNonZeroPos 
    //                   << ", 最后非零值位置: " << lastNonZeroPos << std::endl;
    //     } else {
    //         std::cout << std::endl;
    //     }
        
        // // 检查PCM文件格式 - 按照不同解释方式尝试查看数据
        // std::cout << "[Debug] 尝试不同格式解释PCM数据:" << std::endl;
        
        // // 尝试作为16位整数读取（常见PCM格式）
        // std::cout << "[Debug] 作为16位整数解释首10个样本: ";
        // for (size_t i = 0; i < std::min(bufferSize/2, size_t(10)); ++i) {
        //     const int16_t* int16Ptr = reinterpret_cast<const int16_t*>(buffer) + i;
        //     std::cout << *int16Ptr << " ";
        // }
        // std::cout << std::endl;
        
        // // 尝试作为32位整数读取
        // std::cout << "[Debug] 作为32位整数解释首10个样本: ";
        // for (size_t i = 0; i < std::min(bufferSize/4, size_t(10)); ++i) {
        //     const int32_t* int32Ptr = reinterpret_cast<const int32_t*>(buffer) + i;
        //     std::cout << *int32Ptr << " ";
        // }
        // std::cout << std::endl;
        
        // // 尝试查看内存的前128字节（十六进制表示）
        // std::cout << "[Debug] 内存内容前128字节: " << std::endl;
        // const unsigned char* bytePtr = reinterpret_cast<const unsigned char*>(buffer);
        // for (size_t i = 0; i < std::min(bufferSize * sizeof(float), size_t(128)); ++i) {
        //     std::cout << std::hex << std::setw(2) << std::setfill('0') 
        //               << static_cast<int>(bytePtr[i]) << " ";
        //     if ((i + 1) % 16 == 0) std::cout << std::endl;
        // }
        // std::cout << std::dec << std::endl;
        
        // std::cout << "[警告] 输入音频数据全为零或格式不正确，请检查音频源或数据加载过程" << std::endl;
    // }
}

void AudioDebugger::checkSignatureInput(const float* buffer, size_t bufferSize, 
                                       const std::vector<float>& window) {
    // 检查输入buffer是否有非零值
    bool hasNonZeroInput = false;
    float maxInputVal = 0.0f;
    for (size_t i = 0; i < std::min(bufferSize, size_t(100)); ++i) {
        if (std::abs(buffer[i]) > 0.0001f) {
            hasNonZeroInput = true;
            maxInputVal = std::max(maxInputVal, std::abs(buffer[i]));
        }
    }
    
    std::cout << "[Debug] computeSignaturePoint输入检查: 含非零值: " << (hasNonZeroInput ? "是" : "否") 
              << ", 前100个样本中最大值: " << maxInputVal << std::endl;
    
    if (!hasNonZeroInput) {
        std::cout << "[警告] computeSignaturePoint的输入数据全为零" << std::endl;
        
        std::cout << "[Debug] 尝试打印window_窗口函数值: ";
        for (size_t i = 0; i < std::min(size_t(10), window.size()); ++i) {
            std::cout << window[i] << " ";
        }
        std::cout << "..." << std::endl;
    }
}

void AudioDebugger::checkCopiedBuffer(const std::vector<float>& buffer, size_t offset, size_t maxSize) {
    // // 检查复制到buffer中的数据
    // bool hasNonZeroBuffer = false;
    // for (size_t i = 0; i < std::min(buffer.size(), size_t(100)); ++i) {
    //     if (std::abs(buffer[i]) > 0.0001f) {
    //         hasNonZeroBuffer = true;
    //         break;
    //     }
    // }
    
    // if (!hasNonZeroBuffer) {
    //     std::cout << "[警告] 从偏移量 " << offset << " 复制到buffer_的数据全为零" << std::endl;
    //     if (offset == 0) {
    //         std::cout << "[Debug] buffer_前10个值: ";
    //         for (size_t i = 0; i < std::min(maxSize, size_t(10)); ++i) {
    //             std::cout << buffer[i] << " ";
    //         }
    //         std::cout << std::endl;
    //     }
    // }
}

void AudioDebugger::checkPreEmphasisBuffer(const std::vector<float>& buffer, size_t offset, size_t maxSize) {
    // // 检查预加重后的数据
    // bool hasNonZeroBuffer = false;
    // for (size_t i = 0; i < std::min(buffer.size(), size_t(100)); ++i) {
    //     if (std::abs(buffer[i]) > 0.0001f) {
    //         hasNonZeroBuffer = true;
    //         break;
    //     }
    // }
    
    // if (!hasNonZeroBuffer && offset == 0) {
    //     std::cout << "[警告] 预加重后buffer_中的数据仍为零" << std::endl;
    // }
}

void AudioDebugger::checkFftResults(const std::vector<std::complex<float>>& fftBuffer, 
                                   size_t bufferSize) {
    bool hasNonZeroValue = false;
    float maxFftValue = 0.0f;
    float minFftValue = 0.0f;
    
    for (size_t i = 0; i < bufferSize; ++i) {
        if (std::abs(fftBuffer[i]) > 0.0001f) {
            hasNonZeroValue = true;
            maxFftValue = std::max(maxFftValue, std::abs(fftBuffer[i]));
            if (minFftValue == 0.0f || std::abs(fftBuffer[i]) < minFftValue) {
                minFftValue = std::abs(fftBuffer[i]);
            }
        }
    }
    
    std::cout << "[Debug] FFT结果检查: 含非零值: " << (hasNonZeroValue ? "是" : "否") 
              << ", 最大值: " << maxFftValue 
              << ", 最小非零值: " << minFftValue << std::endl;
    
    if (!hasNonZeroValue) {
        std::cout << "[警告] fftBuffer中所有值接近于零，检查FFT实现或输入数据" << std::endl;
    }
}

void AudioDebugger::checkMagnitudes(const std::vector<float>& magnitudes, 
                                   size_t bufferSize) {
    int nonZeroMags = 0;
    float magSum = 0.0f;
    float maxMagnitude = 0.0f;
    
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        if (magnitudes[i] > 0.0001f) {
            nonZeroMags++;
            magSum += magnitudes[i];
            maxMagnitude = std::max(maxMagnitude, magnitudes[i]);
        }
    }
    
    std::cout << "[Debug] Magnitudes检查: 非零值数量: " << nonZeroMags 
              << ", 平均值: " << (nonZeroMags > 0 ? magSum / nonZeroMags : 0)
              << ", 最大值: " << maxMagnitude << std::endl;
    
    if (nonZeroMags == 0) {
        std::cout << "[警告] magnitudes中所有值为零，问题可能出在FFT结果或对数转换" << std::endl;
    }
}

void AudioDebugger::checkWindowedData(const std::vector<float>& windowed, 
                                     size_t bufferSize) {
    bool hasNonZeroWindowed = false;
    float maxWindowedVal = 0.0f;
    
    for (size_t i = 0; i < std::min(bufferSize, size_t(100)); ++i) {
        if (std::abs(windowed[i]) > 0.0001f) {
            hasNonZeroWindowed = true;
            maxWindowedVal = std::max(maxWindowedVal, std::abs(windowed[i]));
        }
    }
    
    std::cout << "[Debug] 应用窗函数后: 含非零值: " << (hasNonZeroWindowed ? "是" : "否") 
              << ", 前100个样本中最大值: " << maxWindowedVal << std::endl;
    
    if (!hasNonZeroWindowed) {
        std::cout << "[警告] 应用窗函数后数据仍为零" << std::endl;
    }
}

void AudioDebugger::printQuerySignatureStats(const std::vector<SignaturePoint>& querySignature) {
    std::cout << "开始匹配过程，查询指纹点数量: " << querySignature.size() << std::endl;

    // 计算指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueQueryHashes;
    for (const auto& point : querySignature) {
        uniqueQueryHashes.insert(point.hash);
    }
    std::cout << "查询指纹中唯一哈希值数量: " << uniqueQueryHashes.size() << std::endl;
}

void AudioDebugger::printTargetSignatureStats(const std::vector<SignaturePoint>& targetSignature, 
                                             const std::string& title, size_t index) {
    // 计算目标指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueTargetHashes;
    for (const auto& point : targetSignature) {
        uniqueTargetHashes.insert(point.hash);
    }

    std::cout << "比较与 '" << title << "' 的指纹 (目标指纹点数量: " << targetSignature.size() 
              << ", 唯一哈希值: " << uniqueTargetHashes.size() << ")" << std::endl;
    
    // 检查数据库指纹是否完整
    if (targetSignature.empty()) {
        std::cerr << "警告: 数据库中的指纹 #" << index << " (" << title << ") 是空的!" << std::endl;
    }
}

void AudioDebugger::printCommonHashesInfo(const std::unordered_set<uint32_t>& queryHashes, 
                                         const std::unordered_set<uint32_t>& targetHashes) {
    // 检查指纹哈希是否有交集
    std::unordered_set<uint32_t> commonHashes;
    for (const auto& hash : queryHashes) {
        if (targetHashes.count(hash) > 0) {
            commonHashes.insert(hash);
        }
    }
    std::cout << "  共同哈希值数量: " << commonHashes.size() << std::endl;
    
    // 如果有共同哈希，输出它们
    if (!commonHashes.empty() && commonHashes.size() <= 10) {
        std::cout << "  共同哈希值: ";
        for (const auto& hash : commonHashes) {
            std::cout << "0x" << std::hex << hash << std::dec << " ";
        }
        std::cout << std::endl;
    }
}

void AudioDebugger::printSimilarityDebugInfo(size_t totalMatches, double bestOffset, 
                                           size_t maxCount, double confidence,
                                           size_t querySize, size_t targetSize) {
    std::cout << "Debug: Total matches: " << totalMatches
              << ", Best offset: " << bestOffset
              << ", Max count: " << maxCount
              << ", Confidence: " << confidence
              << ", Query size: " << querySize
              << ", Target size: " << targetSize << std::endl;
}

void AudioDebugger::printSignatureDetails(const std::vector<SignaturePoint>& signature, size_t maxItems) {
    std::cout << "  - 指纹点数量: " << signature.size() << std::endl;

    if (!signature.empty()) {
        std::cout << "  - 前" << std::min(maxItems, signature.size()) << "个指纹点:" << std::endl;
        for (size_t i = 0; i < std::min(maxItems, signature.size()); ++i) {
            std::cout << "    [" << i << "] Hash: 0x" 
                     << std::hex << std::setw(8) << std::setfill('0') << signature[i].hash
                     << std::dec << ", Timestamp: " << signature[i].timestamp << std::endl;
        }
    }
    std::cout << std::endl;

    // 计算指纹中的唯一哈希值
    std::unordered_set<uint32_t> uniqueQueryHashes;
    for (const auto& point : signature) {
        uniqueQueryHashes.insert(point.hash);
    }
    std::cout << "唯一哈希值数量: " << uniqueQueryHashes.size() << std::endl;
}

} // namespace afp 