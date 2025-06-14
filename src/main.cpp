#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <iomanip>
#include <set>
#include <map>
#include "afp/afp_interface.h"
#include "debugger/visualization.h"
#include "signature/signature_generator.h"
#include "signature/signature_matcher.h"
#include "matcher/matcher.h"
namespace fs = std::filesystem;

// 可视化文件夹名称
const std::string VISUALIZATION_DIR = "visualization_file_o";

// 创建可视化文件夹并返回完整路径
std::string createVisualizationPath(const std::string& filename) {
    // 创建可视化文件夹（如果不存在）
    if (!fs::exists(VISUALIZATION_DIR)) {
        fs::create_directories(VISUALIZATION_DIR);
        std::cout << "Created visualization directory: " << VISUALIZATION_DIR << std::endl;
    }
    
    // 返回完整路径
    return fs::path(VISUALIZATION_DIR) / filename;
}

// 默认音频格式：16位有符号整数，小端序，单声道，44100Hz
const afp::PCMFormat defaultFormat(44100, 
                                 afp::SampleFormat::S16,
                                 1,
                                 afp::Endianness::Little,
                                 afp::ChannelLayout::Mono);

// 打印指纹信息（用于调试）
void printSignature(const std::vector<afp::SignaturePoint>& signature, const std::string& prefix) {
    std::cout << prefix << " 指纹信息:" << std::endl;
    std::cout << "  - 指纹点数量: " << signature.size() << std::endl;
    
    if (!signature.empty()) {
        std::cout << "  - 前100个指纹点:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(100), signature.size()); ++i) {
            std::cout << "    [" << i << "] Hash: 0x" 
                     << std::hex << std::setw(8) << std::setfill('0') << signature[i].hash
                     << std::dec << ", Timestamp: " << signature[i].timestamp << std::endl;
        }
    }
    
    std::cout << std::endl;
}

// 读取PCM文件
std::vector<uint8_t> readPCMFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return {};
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取原始数据
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    
    return data;
}

// 生成指纹模式
void generateFingerprints(const std::string& algorithm, 
                         const std::string& outputFile,
                         const std::vector<std::string>& inputFiles,
                         bool generateVisualizations = false) {
    // 创建配置和目录 - 生成模式使用高精度配置
    auto config = afp::interface::createPerformanceConfig(afp::PlatformType::Mobile_Gen);
    auto catalog = afp::interface::createCatalog();
    
    for (const auto& inputFile : inputFiles) {
        // 检查文件是否存在
        if (!fs::exists(inputFile)) {
            std::cerr << "Input file does not exist: " << inputFile << std::endl;
            continue;
        }

        std::cout << "Processing: " << inputFile << std::endl;
        
        // 读取PCM数据
        auto buffer = readPCMFile(inputFile);
        if (buffer.empty()) {
            std::cerr << "Failed to read PCM file" << std::endl;
            continue;
        }

        std::cout << "PCM 文件大小: " << buffer.size() << " 字节" << std::endl;

        // 创建生成器并生成指纹
        auto generator = afp::interface::createSignatureGenerator(config);
        
        // Enable visualization if requested
        if (generateVisualizations) {
            auto* generatorImpl = dynamic_cast<afp::SignatureGenerator*>(generator.get());
            if (generatorImpl) {
                generatorImpl->enableVisualization(true);
                generatorImpl->setVisualizationTitle(fs::path(inputFile).stem().string());
                
                // 设置音频文件路径
                generatorImpl->setAudioFilePath(fs::absolute(inputFile).string());
            }
        }
        
        if (!generator->init(defaultFormat)) {
            std::cerr << "Failed to initialize generator" << std::endl;
            continue;
        }

        if (!generator->appendStreamBuffer(buffer.data(), buffer.size(), 0.0)) {
            std::cerr << "Failed to generate signature" << std::endl;
            continue;
        }

        // 打印生成的指纹信息
        printSignature(generator->signature(), "生成");

        // 创建媒体信息
        afp::MediaItem mediaItem;
        mediaItem.setTitle(fs::path(inputFile).stem().string());
        mediaItem.setSubtitle("Generated from PCM file");
        mediaItem.setChannelCount(defaultFormat.channels());

        // 添加到目录
        catalog->addSignature(generator->signature(), mediaItem);
        
        // Save visualization if requested
        if (generateVisualizations) {
            auto* generatorImpl = dynamic_cast<afp::SignatureGenerator*>(generator.get());
            if (generatorImpl) {
                std::string vizFilename = fs::path(inputFile).stem().string() + "_fingerprint.json";
                std::string vizPath = createVisualizationPath(vizFilename);
                std::cout << "Generating visualization: " << vizPath << std::endl;
                generatorImpl->saveVisualization(vizPath);
            }
        }
    }

    // 保存到文件
    if (!catalog->saveToFile(outputFile)) {
        std::cerr << "Failed to save catalog" << std::endl;
        return;
    }

    std::cout << "Fingerprints saved to: " << outputFile << std::endl;
    std::cout << "总共保存了 " << catalog->signatures().size() << " 个指纹" << std::endl;
}

// 匹配指纹模式
void matchFingerprints(const std::string& algorithm, 
                      const std::string& catalogFile, 
                      const std::vector<std::string>& inputFiles, 
                      bool generateVisualizations = false) {
    // 创建配置和目录 - 匹配模式使用平衡配置
    auto config = afp::interface::createPerformanceConfig(afp::PlatformType::Mobile);
    auto catalog = afp::interface::createCatalog();

    // 加载目录
    if (!catalog->loadFromFile(catalogFile)) {
        std::cerr << "Failed to load catalog" << std::endl;
        return;
    }

    std::cout << "已加载指纹数据库: " << catalogFile << std::endl;
    std::cout << "数据库中指纹数量: " << catalog->signatures().size() << std::endl;
    
    // Store source fingerprint visualization data if visualization is enabled
    afp::VisualizationData sourceVizData;
    bool sourceVizEnabled = false;
    
    // 打印加载的指纹信息
    for (size_t i = 0; i < catalog->signatures().size(); ++i) {
        std::cout << "数据库中指纹 #" << i << " (" << catalog->mediaItems()[i].title() << "):" << std::endl;
        printSignature(catalog->signatures()[i], "数据库");
        
        // If visualization is enabled, create source visualization data
        if (generateVisualizations && i == 0) {  // Just using the first signature for simplicity
            sourceVizEnabled = true;
            sourceVizData.title = catalog->mediaItems()[i].title();
            sourceVizData.duration = 0.0;
            
            // Add fingerprint points
            for (const auto& point : catalog->signatures()[i]) {
                sourceVizData.fingerprintPoints.emplace_back(point.frequency, point.timestamp, point.hash);
                sourceVizData.allPeaks.emplace_back(point.frequency, point.timestamp, point.amplitude / 1000.0f);
                
                // Update duration
                if (point.timestamp > sourceVizData.duration) {
                    sourceVizData.duration = point.timestamp;
                }
            }
            
            // Add buffer to duration
            sourceVizData.duration += 1.0;
            
            // 保存源音频的绝对路径（即使在数据库中没有该路径，但是可以从文件名尝试猜测）
            std::string sourceAudioPath = ""; // 将来的扩展：尝试找到对应的源音频文件
            sourceVizData.audioFilePath = sourceAudioPath;
            
            // Save source visualization
            std::string sourceVizFilename = catalog->mediaItems()[i].title() + "_source.json";
            std::string sourceVizPath = createVisualizationPath(sourceVizFilename);
            afp::Visualizer::saveVisualization(sourceVizData, sourceVizPath);
        }
    }

    // 跟踪哪些文件已匹配和未匹配
    std::set<std::string> matchedFiles;
    std::set<std::string> unmatchedFiles;
    std::map<std::string, std::string> matchDetails; // 文件名 -> 匹配详情
    
    // 处理每个输入文件
    for (const auto& inputFile : inputFiles) {
        // 检查文件是否存在
        if (!fs::exists(inputFile)) {
            std::cerr << "Input file does not exist: " << inputFile << std::endl;
            unmatchedFiles.insert(inputFile);
            continue;
        }
        
        std::cout << "\n开始匹配文件: " << inputFile << std::endl;
        
        // 获取输入PCM文件的绝对路径
        std::string inputFileAbsPath = fs::absolute(inputFile).string();
        
        // 创建匹配器
        auto matcher = afp::interface::createMatcher(catalog, config, defaultFormat);
        
        // 标记当前文件是否匹配
        bool currentFileMatched = false;
        std::string matchInfo;
        
        // Enable visualization if requested
        if (generateVisualizations) {
            auto* matcherImpl = dynamic_cast<afp::Matcher*>(matcher.get());
            if (matcherImpl) {
                matcherImpl->signatureMatcher_->enableVisualization(true);
                matcherImpl->signatureMatcher_->setVisualizationTitle(fs::path(inputFile).stem().string());
                
                // 设置查询音频文件路径（PCM文件）
                matcherImpl->signatureMatcher_->setAudioFilePath(inputFileAbsPath);
            }
        }
        
        matcher->setMatchCallback([&](const afp::MatchResult& result) {
            currentFileMatched = true;
            
            // 保存匹配信息
            std::stringstream ss;
            ss << "匹配: " << result.mediaItem->title()
               << ", 偏移: " << std::fixed << std::setprecision(2) << result.offset << "秒"
               << ", 置信度: " << std::fixed << std::setprecision(3) << result.confidence
               << ", 匹配点数: " << result.matchCount
               << ", 唯一时间戳匹配数: " << result.uniqueTimestampMatchCount;
            
            matchInfo = ss.str();
            
            std::cout << "Match found:" << std::endl;
            std::cout << "  Title: " << result.mediaItem->title() << std::endl;
            std::cout << "  Offset: " << result.offset << " seconds" << std::endl;
            std::cout << "  Confidence: " << result.confidence << std::endl;
            std::cout << "  Matched points: " << result.matchedPoints.size() << std::endl;
            std::cout << "  Matched count: " << result.matchCount << std::endl;
            std::cout << "  Unique timestamp match count: " << result.uniqueTimestampMatchCount << std::endl;
            std::cout << std::endl;
        });

        std::cout << "Matching: " << inputFile << std::endl;
        
        // 读取PCM数据
        auto buffer = readPCMFile(inputFile);
        if (buffer.empty()) {
            std::cerr << "Failed to read PCM file" << std::endl;
            unmatchedFiles.insert(inputFile);
            continue;
        }

        std::cout << "待匹配PCM文件大小: " << buffer.size() << " 字节" << std::endl;

        // 执行匹配
        if (!matcher->appendStreamBuffer(buffer.data(), buffer.size(), 0.0)) {
            std::cerr << "Failed to match signature" << std::endl;
            unmatchedFiles.insert(inputFile);
            continue;
        }
        
        // 更新匹配/未匹配列表
        if (currentFileMatched) {
            matchedFiles.insert(inputFile);
            matchDetails[inputFile] = matchInfo;
        } else {
            unmatchedFiles.insert(inputFile);
        }
        
        // Generate visualizations if requested
        if (generateVisualizations) {
            auto* matcherImpl = dynamic_cast<afp::Matcher*>(matcher.get());
            if (matcherImpl) {
                // Generate query visualization
                std::string queryVizFilename = fs::path(inputFile).stem().string() + "_query.json";
                std::string queryVizPath = createVisualizationPath(queryVizFilename);
                matcherImpl->signatureMatcher_->setAudioFilePath(inputFileAbsPath);
                matcherImpl->signatureMatcher_->saveVisualization(queryVizPath);
                
                // Generate comparison visualization if source data is available
                if (sourceVizEnabled) {
                    std::string comparisonBasename = "comparison_" + fs::path(inputFile).stem().string() + "_vs_source";
                    std::string sourceFilename = createVisualizationPath(comparisonBasename + "_source.json");
                    std::string queryFilename = createVisualizationPath(comparisonBasename + "_query.json");
                    std::string sessionsFilename = createVisualizationPath(comparisonBasename + "_sessions.json");
                    
                    // 设置查询数据的音频文件路径
                    afp::VisualizationData queryVizData = matcherImpl->signatureMatcher_->getVisualizationData();
                    queryVizData.audioFilePath = inputFileAbsPath;
                    
                    matcherImpl->signatureMatcher_->saveComparisonData(
                        sourceVizData, 
                        sourceFilename,
                        queryFilename,
                        sessionsFilename
                    );
                    
                    // 保存会话数据以供交互式可视化使用
                    matcherImpl->signatureMatcher_->saveSessionsData(sessionsFilename);
                    
                    std::cout << "Visualization data saved to " << VISUALIZATION_DIR << " with audio path: " << inputFileAbsPath << std::endl;
                }
            }
        }
    }
    
    // 输出匹配和未匹配文件的统计信息
    std::cout << "\n==== 匹配结果统计 ====\n" << std::endl;
    
    std::cout << "匹配成功的文件 (" << matchedFiles.size() << "个):" << std::endl;
    for (const auto& file : matchedFiles) {
        std::cout << "  - " << file;
        if (matchDetails.find(file) != matchDetails.end()) {
            std::cout << " (" << matchDetails[file] << ")";
        }
        std::cout << std::endl;
    }
    
    std::cout << "\n未匹配成功的文件 (" << unmatchedFiles.size() << "个):" << std::endl;
    for (const auto& file : unmatchedFiles) {
        std::cout << "  - " << file << std::endl;
    }
    
    std::cout << "\n总处理文件数: " << (matchedFiles.size() + unmatchedFiles.size()) 
              << ", 匹配成功率: " << (inputFiles.empty() ? 0 : (static_cast<double>(matchedFiles.size()) / inputFiles.size() * 100))
              << "%" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  Generate fingerprints: " << argv[0] << " generate <algorithm> <output_file> <input_file1> [input_file2 ...] [--visualize]" << std::endl;
        std::cerr << "  Match fingerprints: " << argv[0] << " match <algorithm> <catalog_file> <input_file1> [input_file2 ...] [--visualize]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    bool visualize = false;
    
    // Check for visualization flag
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--visualize") {
            visualize = true;
            break;
        }
    }
    
    if (mode == "generate") {
        if (argc < 5) {
            std::cerr << "Error: Not enough arguments for generate mode" << std::endl;
            return 1;
        }
        std::string algorithm = argv[2];
        std::string outputFile = argv[3];
        std::vector<std::string> inputFiles;
        for (int i = 4; i < argc; ++i) {
            if (std::string(argv[i]) != "--visualize") {
                inputFiles.push_back(argv[i]);
            }
        }
        
        std::cout << "将生成指纹保存到: " << outputFile << std::endl;
        for (const auto& file : inputFiles) {
            std::cout << "处理文件: " << file << std::endl;
        }
        
        generateFingerprints(algorithm, outputFile, inputFiles, visualize);
        std::cout << "指纹生成完成，已保存到: " << outputFile << std::endl;
        
    } else if (mode == "match") {
        if (argc < 5) {
            std::cerr << "Error: Not enough arguments for match mode" << std::endl;
            return 1;
        }
        std::string algorithm = argv[2];
        std::string catalogFile = argv[3];
        std::vector<std::string> inputFiles;
        
        // 收集所有输入文件
        for (int i = 4; i < argc; ++i) {
            if (std::string(argv[i]) != "--visualize") {
                inputFiles.push_back(argv[i]);
            }
        }
        
        if (inputFiles.empty()) {
            std::cerr << "Error: No input files specified for matching" << std::endl;
            return 1;
        }
        
        std::cout << "正在加载指纹数据库..." << std::endl;
        std::cout << "将处理 " << inputFiles.size() << " 个音频文件..." << std::endl;
        
        if (algorithm != "shazam") {
            std::cerr << "Error: Currently only 'shazam' algorithm is supported for matching" << std::endl;
            return 1;
        }
        
        matchFingerprints(algorithm, catalogFile, inputFiles, visualize);
        std::cout << "所有文件处理完成!" << std::endl;
        
    } else {
        std::cerr << "Invalid mode: " << mode << std::endl;
        return 1;
    }

    return 0;
} 
