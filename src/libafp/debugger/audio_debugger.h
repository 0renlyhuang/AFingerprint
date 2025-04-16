#pragma once
#include <vector>
#include <complex>
#include <unordered_set>
#include <cstdint>
#include <string>

namespace afp {

// Forward declaration
struct SignaturePoint;

// 音频调试工具类
class AudioDebugger {
public:
    // 检查音频缓冲区是否包含非零值，并输出详细的调试信息
    static void checkAudioBuffer(const float* buffer, size_t bufferSize, 
                                double startTimestamp, bool isFirstCall = false);
    
    // 检查computeSignaturePoint方法的输入buffer数据
    static void checkSignatureInput(const float* buffer, size_t bufferSize, 
                                   const std::vector<float>& window);
    
    // 检查拷贝到buffer_的数据
    static void checkCopiedBuffer(const std::vector<float>& buffer, size_t offset, size_t maxSize);
    
    // 检查预加重后的数据
    static void checkPreEmphasisBuffer(const std::vector<float>& buffer, size_t offset, size_t maxSize);
    
    // 检查FFT处理后数据
    static void checkFftResults(const std::vector<std::complex<float>>& fftBuffer, 
                               size_t bufferSize);
    
    // 检查幅度谱数据                           
    static void checkMagnitudes(const std::vector<float>& magnitudes, 
                               size_t bufferSize);
                               
    // 检查窗口函数应用后的数据
    static void checkWindowedData(const std::vector<float>& windowed, 
                                 size_t bufferSize);
                                 
    // 指纹调试功能：打印查询指纹的统计信息
    static void printQuerySignatureStats(const std::vector<SignaturePoint>& querySignature);
    
    // 指纹调试功能：打印目标指纹的统计信息
    static void printTargetSignatureStats(const std::vector<SignaturePoint>& targetSignature, 
                                         const std::string& title, size_t index = 0);
    
    // 指纹调试功能：打印哈希交集信息
    static void printCommonHashesInfo(const std::unordered_set<uint32_t>& queryHashes, 
                                    const std::unordered_set<uint32_t>& targetHashes);
                                    
    // 指纹调试功能：打印相似度计算的详细信息
    static void printSimilarityDebugInfo(size_t totalMatches, double bestOffset, 
                                        size_t maxCount, double confidence,
                                        size_t querySize, size_t targetSize);
                                        
    // 打印指纹详细信息
    static void printSignatureDetails(const std::vector<SignaturePoint>& signature, size_t maxItems = 10);
};

} // namespace afp 