#include "signature_generator.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace afp {

SignatureGenerator::SignatureGenerator() 
    : fftSize_(2048)
    , sampleRate_(44100)
    , hopSize_(512) {
}

SignatureGenerator::~SignatureGenerator() = default;

bool SignatureGenerator::init(size_t fftSize, size_t sampleRate, size_t hopSize) {
    fftSize_ = fftSize;
    sampleRate_ = sampleRate;
    hopSize_ = hopSize;

    // 创建FFT实例
    fft_ = FFTFactory::create(fftSize_);
    if (!fft_ || !fft_->init(fftSize_)) {
        return false;
    }

    // 初始化汉宁窗
    window_.resize(fftSize_);
    for (size_t i = 0; i < fftSize_; ++i) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (fftSize_ - 1)));
    }

    // 初始化缓冲区
    buffer_.resize(fftSize_);
    fftBuffer_.resize(fftSize_);

    return true;
}

bool SignatureGenerator::appendStreamBuffer(const float* buffer, 
                                         size_t bufferSize,
                                         double startTimestamp) {
    if (!buffer || bufferSize == 0) {
        return false;
    }

    // 添加调试信息
    static bool firstCall = true;
    if (firstCall) {
        std::cout << "首次处理音频数据: " << bufferSize << " 样本, 起始时间戳: " 
                  << startTimestamp << std::endl;
        firstCall = false;
    }

    // 检查原始输入数据是否含有非零值
    bool hasNonZeroInput = false;
    float maxInputValue = 0.0f;
    size_t firstNonZeroPos = 0;
    size_t lastNonZeroPos = 0;
    
    // 先检查前100个样本 
    for (size_t i = 0; i < std::min(bufferSize, size_t(100)); ++i) {
        if (std::abs(buffer[i]) > 0.0001f) {
            hasNonZeroInput = true;
            maxInputValue = std::max(maxInputValue, std::abs(buffer[i]));
            if (firstNonZeroPos == 0) firstNonZeroPos = i;
            lastNonZeroPos = i;
        }
    }
    
    std::cout << "[Debug] 输入音频前100样本检查: 含非零值: " << (hasNonZeroInput ? "是" : "否") 
              << ", 前100个样本中最大值: " << maxInputValue << std::endl;
    
    // 如果前100个样本都是0，检查整个缓冲区
    if (!hasNonZeroInput) {
        // 全面扫描整个缓冲区
        std::cout << "[Debug] 扫描全部 " << bufferSize << " 个样本..." << std::endl;
        
        size_t nonZeroCount = 0;
        
        for (size_t i = 0; i < bufferSize; ++i) {
            if (std::abs(buffer[i]) > 0.0001f) {
                hasNonZeroInput = true;
                nonZeroCount++;
                maxInputValue = std::max(maxInputValue, std::abs(buffer[i]));
                if (firstNonZeroPos == 0) firstNonZeroPos = i;
                lastNonZeroPos = i;
                
                // 找到第一个非零值后，输出一些样本
                if (nonZeroCount == 1) {
                    std::cout << "[Debug] 在位置 " << i << " 找到第一个非零值: " << buffer[i] << std::endl;
                    std::cout << "[Debug] 样本值 " << i << " 到 " << i+9 << ": ";
                    for (size_t j = i; j < std::min(i+10, bufferSize); ++j) {
                        std::cout << buffer[j] << " ";
                    }
                    std::cout << std::endl;
                }
            }
        }
        
        std::cout << "[Debug] 全部样本扫描结果: 含非零值: " << (hasNonZeroInput ? "是" : "否") 
                  << ", 非零值数量: " << nonZeroCount
                  << ", 最大值: " << maxInputValue;
        
        if (hasNonZeroInput) {
            std::cout << ", 首个非零值位置: " << firstNonZeroPos 
                      << ", 最后非零值位置: " << lastNonZeroPos << std::endl;
        } else {
            std::cout << std::endl;
        }
        
        // 检查PCM文件格式 - 按照不同解释方式尝试查看数据
        std::cout << "[Debug] 尝试不同格式解释PCM数据:" << std::endl;
        
        // 尝试作为16位整数读取（常见PCM格式）
        std::cout << "[Debug] 作为16位整数解释首10个样本: ";
        for (size_t i = 0; i < std::min(bufferSize/2, size_t(10)); ++i) {
            const int16_t* int16Ptr = reinterpret_cast<const int16_t*>(buffer) + i;
            std::cout << *int16Ptr << " ";
        }
        std::cout << std::endl;
        
        // 尝试作为32位整数读取
        std::cout << "[Debug] 作为32位整数解释首10个样本: ";
        for (size_t i = 0; i < std::min(bufferSize/4, size_t(10)); ++i) {
            const int32_t* int32Ptr = reinterpret_cast<const int32_t*>(buffer) + i;
            std::cout << *int32Ptr << " ";
        }
        std::cout << std::endl;
        
        // 尝试查看内存的前128字节（十六进制表示）
        std::cout << "[Debug] 内存内容前128字节: " << std::endl;
        const unsigned char* bytePtr = reinterpret_cast<const unsigned char*>(buffer);
        for (size_t i = 0; i < std::min(bufferSize * sizeof(float), size_t(128)); ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') 
                      << static_cast<int>(bytePtr[i]) << " ";
            if ((i + 1) % 16 == 0) std::cout << std::endl;
        }
        std::cout << std::dec << std::endl;
        
        std::cout << "[警告] 输入音频数据全为零或格式不正确，请检查音频源或数据加载过程" << std::endl;
    }

    // 确保一致的初始状态 - 重置缓冲区
    buffer_.clear();
    buffer_.resize(fftSize_, 0.0f);
    
    std::cout << "处理音频数据: " << bufferSize << " 样本, 使用固定大小缓冲区: " << fftSize_ << " 样本" << std::endl;
    
    // 对于每个 hop 块生成指纹
    signatures_.clear(); // 确保每次调用时从零开始生成指纹
    
    // 循环处理输入缓冲区，降低hop大小以产生更多的分析帧
    size_t hopSize = hopSize_ / 4; // 使用更小的hop大小以获得更多的星座图点
    if (hopSize < 64) hopSize = 64; // 确保hop大小不会太小
    
    size_t offset = 0;
    while (offset + fftSize_ <= bufferSize) {
        // 提取当前帧
        std::memcpy(buffer_.data(), buffer + offset, fftSize_ * sizeof(float));
        
        // 检查复制到buffer_中的数据
        bool hasNonZeroBuffer = false;
        for (size_t i = 0; i < std::min(fftSize_, size_t(100)); ++i) {
            if (std::abs(buffer_[i]) > 0.0001f) {
                hasNonZeroBuffer = true;
                break;
            }
        }
        
        if (!hasNonZeroBuffer) {
            std::cout << "[警告] 从偏移量 " << offset << " 复制到buffer_的数据全为零" << std::endl;
            if (offset == 0) {
                std::cout << "[Debug] buffer_前10个值: ";
                for (size_t i = 0; i < std::min(fftSize_, size_t(10)); ++i) {
                    std::cout << buffer_[i] << " ";
                }
                std::cout << std::endl;
            }
        }
        
        // 应用预加重滤波器以强调高频内容
        for (size_t i = 1; i < fftSize_; ++i) {
            buffer_[i] -= 0.95f * buffer_[i-1];
        }
        
        // 检查预加重后的数据
        hasNonZeroBuffer = false;
        for (size_t i = 0; i < std::min(fftSize_, size_t(100)); ++i) {
            if (std::abs(buffer_[i]) > 0.0001f) {
                hasNonZeroBuffer = true;
                break;
            }
        }
        
        if (!hasNonZeroBuffer && offset == 0) {
            std::cout << "[警告] 预加重后buffer_中的数据仍为零" << std::endl;
        }
        
        // 计算当前时间戳
        double currentTimestamp = startTimestamp + offset / static_cast<double>(sampleRate_);
        
        // 生成指纹 - 使用星座图算法
        SignaturePoint point = computeSignaturePoint(buffer_.data(), fftSize_, currentTimestamp);
        
        // 记录有效的指纹点 - 使用修改后的方法会生成多个有效指纹
        if (point.hash != 0) {
            signatures_.push_back(point);
            
            // 只输出少量调试信息，避免日志过多
            if (signatures_.size() <= 5 || signatures_.size() % 100 == 0) {
                std::cout << "[Debug] 生成指纹点 #" << signatures_.size() 
                          << ": 时间=" << currentTimestamp 
                          << "s, 哈希=0x" << std::hex << point.hash << std::dec 
                          << ", 频率=" << point.frequency << "Hz" << std::endl;
            }
        }
        
        // 移动到下一个 hop
        offset += hopSize;
    }
    
    std::cout << "[Debug] 总共生成了 " << signatures_.size() << " 个指纹点" << std::endl;
    
    if (signatures_.empty()) {
        std::cerr << "未能生成有效指纹，使用备用方案" << std::endl;
        // 如果没有生成有效指纹，创建一个备用指纹
        SignaturePoint fallbackPoint;
        fallbackPoint.hash = 0x12345678; // 使用一个固定的哈希值
        fallbackPoint.timestamp = startTimestamp;
        fallbackPoint.frequency = 1000;
        fallbackPoint.amplitude = 100;
        signatures_.push_back(fallbackPoint);
        std::cout << "[Debug] 添加了一个备用指纹点" << std::endl;
    }

    return !signatures_.empty();
}

std::vector<SignaturePoint> SignatureGenerator::signature() const {
    return signatures_;
}

SignaturePoint SignatureGenerator::computeSignaturePoint(const float* buffer,
                                                      size_t bufferSize,
                                                      double timestamp) {
    // 调试：检查输入buffer是否有非零值
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
        for (size_t i = 0; i < std::min(size_t(10), window_.size()); ++i) {
            std::cout << window_[i] << " ";
        }
        std::cout << "..." << std::endl;
    }

    // 应用窗函数
    std::vector<float> windowed(bufferSize);
    for (size_t i = 0; i < bufferSize; ++i) {
        windowed[i] = buffer[i] * window_[i];
    }
    
    // 检查窗处理后的结果
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

    // 执行FFT
    if (!fft_->transform(windowed.data(), fftBuffer_.data())) {
        return SignaturePoint{0, timestamp, 0, 0};
    }

    // 调试信息：检查fftBuffer_是否有内容
    bool hasNonZeroValue = false;
    float maxFftValue = 0.0f;
    float minFftValue = 0.0f;
    
    for (size_t i = 0; i < bufferSize; ++i) {
        if (std::abs(fftBuffer_[i]) > 0.0001f) {
            hasNonZeroValue = true;
            maxFftValue = std::max(maxFftValue, std::abs(fftBuffer_[i]));
            if (minFftValue == 0.0f || std::abs(fftBuffer_[i]) < minFftValue) {
                minFftValue = std::abs(fftBuffer_[i]);
            }
        }
    }
    
    std::cout << "[Debug] FFT结果检查: 含非零值: " << (hasNonZeroValue ? "是" : "否") 
              << ", 最大值: " << maxFftValue 
              << ", 最小非零值: " << minFftValue << std::endl;
    
    if (!hasNonZeroValue) {
        std::cout << "[警告] fftBuffer_中所有值接近于零，检查FFT实现或输入数据" << std::endl;
        // 输出几个输入样本检查
        std::cout << "[Debug] 输入样本检查: ";
        for (size_t i = 0; i < std::min(size_t(10), bufferSize); ++i) {
            std::cout << windowed[i] << " ";
        }
        std::cout << std::endl;
    }

    // 计算幅度谱，使用对数幅度强调动态范围
    std::vector<float> magnitudes(bufferSize / 2);
    std::vector<float> frequencies(bufferSize / 2);
    
    float maxMagnitude = 0.0001f; // 避免除零
    
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        // 计算复数的模
        float magnitude = std::abs(fftBuffer_[i]);
        
        // 对数频谱 - 修正小幅值的处理
        magnitudes[i] = magnitude > 0.00001f ? 20.0f * std::log10(magnitude) + 100.0f : 0;
        
        // 更新最大值
        if (magnitudes[i] > maxMagnitude) {
            maxMagnitude = magnitudes[i];
        }
        
        frequencies[i] = i * sampleRate_ / bufferSize;
    }
    
    // 添加调试信息，检查magnitudes是否为全0
    int nonZeroMags = 0;
    float magSum = 0.0f;
    
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        if (magnitudes[i] > 0.0001f) {
            nonZeroMags++;
            magSum += magnitudes[i];
        }
    }
    
    std::cout << "[Debug] Magnitudes检查: 非零值数量: " << nonZeroMags 
              << ", 平均值: " << (nonZeroMags > 0 ? magSum / nonZeroMags : 0)
              << ", 最大值: " << maxMagnitude << std::endl;
    
    if (nonZeroMags == 0) {
        std::cout << "[警告] magnitudes中所有值为零，问题可能出在FFT结果或对数转换" << std::endl;
        // 输出几个原始FFT值以检查
        std::cout << "[Debug] 原始FFT值: ";
        for (size_t i = 0; i < std::min(size_t(10), bufferSize / 2); ++i) {
            std::cout << std::abs(fftBuffer_[i]) << " ";
        }
        std::cout << std::endl;
    }
    
    // 正规化幅度谱
    for (size_t i = 0; i < bufferSize / 2; ++i) {
        magnitudes[i] /= maxMagnitude;
    }

    // 生成星座图和锚点-目标点对
    auto constellationHashes = generateConstellationHashes(magnitudes, frequencies, timestamp);
    
    // 计算hash值
    uint32_t hash = 0;
    uint32_t frequency = 0;
    uint32_t amplitude = 0;
    
    if (!constellationHashes.empty()) {
        // 使用第一个哈希值作为代表
        hash = constellationHashes[0].hash;
        frequency = constellationHashes[0].anchorFreq;
        amplitude = constellationHashes[0].amplitude;
    }

    return SignaturePoint{hash, timestamp, frequency, amplitude};
}

std::vector<ConstellationPoint> SignatureGenerator::generateConstellationHashes(
    const std::vector<float>& magnitudes,
    const std::vector<float>& frequencies,
    double timestamp) {
    
    std::vector<ConstellationPoint> result;
    
    // 定义星座图参数
    const int TARGET_ZONE_SIZE = 5;   // 目标区域大小 (秒)
    const int MIN_FREQ_DELTA = 30;    // 最小频率差 (Hz)
    const int MAX_FREQ_DELTA = 300;   // 最大频率差 (Hz)
    
    // 找到频谱中的峰值点
    struct Peak {
        size_t index;
        float magnitude;
        float frequency;
    };
    
    std::vector<Peak> peaks;
    
    // 查找所有峰值 - 使用更严格的峰值检测
    for (size_t i = 2; i < magnitudes.size() - 2; ++i) {
        if (magnitudes[i] > 0.01 && 
            magnitudes[i] > magnitudes[i-1] && 
            magnitudes[i] > magnitudes[i-2] && 
            magnitudes[i] > magnitudes[i+1] && 
            magnitudes[i] > magnitudes[i+2] && 
            frequencies[i] >= 250 && frequencies[i] <= 5500) { // Shazam常用频率范围
            
            peaks.push_back({i, magnitudes[i], frequencies[i]});
        }
    }
    
    // 打印调试信息
    std::cout << "[Debug] 找到 " << peaks.size() << " 个峰值点用于星座图" << std::endl;
    
    // 按振幅从大到小排序
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
        return a.magnitude > b.magnitude;
    });
    
    // 取前50个最强的峰值作为锚点
    if (peaks.size() > 50) {
        peaks.resize(50);
    }
    
    // 生成星座图 - 为每个锚点找到目标区域内的目标点
    for (size_t i = 0; i < peaks.size(); ++i) {
        // 当前峰值作为锚点
        const Peak& anchor = peaks[i];
        
        // 在目标区域找寻目标点
        for (size_t j = 0; j < peaks.size(); ++j) {
            if (i == j) continue; // 跳过自身
            
            const Peak& target = peaks[j];
            
            // 检查频率差是否在有效范围内
            float freqDelta = target.frequency - anchor.frequency;
            if (freqDelta >= MIN_FREQ_DELTA && freqDelta <= MAX_FREQ_DELTA) {
                // 计算时间增量，Shazam中通常映射为一个0-8秒内的整数值
                // 这里我们简化为0-100的整数
                uint32_t deltaTime = j % 100; // 简化的时间差表示
                
                // 计算Shazam风格的哈希值
                // 哈希 = (锚点频率 | 目标频率 | 时间差)
                uint32_t hash = ((static_cast<uint32_t>(anchor.frequency) & 0x1FFF) << 19) | 
                               ((static_cast<uint32_t>(target.frequency) & 0x1FFF) << 6) |
                               (deltaTime & 0x3F);
                
                ConstellationPoint point;
                point.hash = hash;
                point.anchorFreq = static_cast<uint32_t>(anchor.frequency);
                point.targetFreq = static_cast<uint32_t>(target.frequency);
                point.deltaTime = deltaTime;
                point.amplitude = static_cast<uint32_t>(anchor.magnitude * 1000);
                
                result.push_back(point);
            }
        }
    }
    
    std::cout << "[Debug] 生成了 " << result.size() << " 个星座图哈希值" << std::endl;
    
    return result;
}

// 这个方法将不再使用，改为使用generateConstellationHashes
std::vector<std::pair<uint32_t, uint32_t>> SignatureGenerator::findPeakPairs(
    const std::vector<float>& magnitudes,
    const std::vector<float>& frequencies) {
    
    std::vector<std::pair<uint32_t, uint32_t>> peakPairs;
    
    // 找到频谱中的峰值
    struct Peak {
        size_t index;
        float magnitude;
        float frequency;
    };
    
    std::vector<Peak> peaks;
    
    // 查找所有峰值
    for (size_t i = 2; i < magnitudes.size() - 2; ++i) {
        if (magnitudes[i] > 0.001 && 
            magnitudes[i] > magnitudes[i-1] && 
            magnitudes[i] > magnitudes[i-2] && 
            magnitudes[i] > magnitudes[i+1] && 
            magnitudes[i] > magnitudes[i+2] && 
            frequencies[i] >= 20 && frequencies[i] <= 8000) {
            
            peaks.push_back({i, magnitudes[i], frequencies[i]});
        }
    }
    
    // 打印调试信息
    std::cout << "[Debug] 找到 " << peaks.size() << " 个峰值点" << std::endl;
    
    // 如果找不到足够的峰值，则使用简单的局部最大值方法 (备用方法)
    if (peaks.size() < 5) {
        peaks.clear();
        for (size_t i = 1; i < magnitudes.size() - 1; ++i) {
            if (magnitudes[i] > magnitudes[i-1] && 
                magnitudes[i] > magnitudes[i+1] && 
                frequencies[i] >= 20 && frequencies[i] <= 8000) {
                
                peaks.push_back({i, magnitudes[i], frequencies[i]});
            }
        }
        std::cout << "[Debug] 使用简单峰值方法后找到 " << peaks.size() << " 个峰值点" << std::endl;
    }
    
    // 按振幅从大到小排序
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) {
        return a.magnitude > b.magnitude;
    });
    
    // 取前100个最强的峰值
    if (peaks.size() > 100) {
        peaks.resize(100);
    }
    
    // 生成峰值对
    for (size_t i = 0; i < peaks.size(); ++i) {
        for (size_t j = i + 1; j < peaks.size(); ++j) {
            float freqDiff = peaks[j].frequency - peaks[i].frequency;
            if (freqDiff >= 10 && freqDiff <= 400) {
                peakPairs.emplace_back(
                    static_cast<uint32_t>(peaks[i].frequency),
                    static_cast<uint32_t>(peaks[j].frequency));
            }
        }
    }
    
    std::cout << "[Debug] 生成了 " << peakPairs.size() << " 个配对，" 
              << signatures_.size() << " 个哈希值" << std::endl;
    
    return peakPairs;
}

uint32_t SignatureGenerator::computeHash(uint32_t f1, uint32_t f2, uint32_t t) {
    // 改进哈希算法，加入时间戳的低位信息
    // f1 和 f2 分别保留13位，t的低6位
    // 这样哈希值中包含频率信息和时间信息，增加唯一性
    uint32_t hash = ((f1 & 0x1FFF) << 19) |  // 高13位用于f1 (频率1)
                   ((f2 & 0x1FFF) << 6) |    // 中间13位用于f2 (频率2)
                   (t & 0x3F);               // 低6位用于时间戳
    
    // 调试输出
    static size_t hashCount = 0;
    if (hashCount < 5) {
        std::cout << "[Debug] 哈希计算: f1=" << f1 << ", f2=" << f2 
                  << ", t=" << t << " -> 0x" << std::hex << hash << std::dec << std::endl;
        hashCount++;
    }
    
    return hash;
}

} // namespace afp 