#include "debugger/visualization.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <unordered_map>
#include <vector>

namespace afp {

Visualizer::Visualizer() {}

Visualizer::~Visualizer() {}

// Singleton instance
Visualizer& Visualizer::getInstance() {
    static Visualizer instance;
    return instance;
}

bool Visualizer::saveVisualization(const VisualizationData& data, const std::string& filename) {
    // Create and open a JSON file to save the data
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    // Write JSON header
    file << "{\n";
    file << "  \"title\": \"" << data.title << "\",\n";
    file << "  \"duration\": " << data.duration << ",\n";
    
    // Write audio file path if available
    if (!data.audioFilePath.empty()) {
        file << "  \"audioFilePath\": \"" << data.audioFilePath << "\",\n";
    }
    
    // Write peaks
    file << "  \"allPeaks\": [\n";
    for (size_t i = 0; i < data.allPeaks.size(); ++i) {
        file << "    [" << std::get<0>(data.allPeaks[i]) << ", " 
             << std::get<1>(data.allPeaks[i]) << ", "
             << std::get<2>(data.allPeaks[i]) << "]";
        if (i < data.allPeaks.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    
    // Write fingerprint points
    file << "  \"fingerprintPoints\": [\n";
    for (size_t i = 0; i < data.fingerprintPoints.size(); ++i) {
        file << "    [" << std::get<0>(data.fingerprintPoints[i]) << ", " 
             << std::get<1>(data.fingerprintPoints[i]) << ", "
             << "\"0x" << std::hex << std::get<2>(data.fingerprintPoints[i]) << std::dec << "\"]";
        if (i < data.fingerprintPoints.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ]";
    
    // Write matched points if available
    if (!data.matchedPoints.empty()) {
        file << ",\n  \"matchedPoints\": [\n";
        for (size_t i = 0; i < data.matchedPoints.size(); ++i) {
            file << "    [" << std::get<0>(data.matchedPoints[i]) << ", " 
                 << std::get<1>(data.matchedPoints[i]) << ", "
                 << "\"0x" << std::hex << std::get<2>(data.matchedPoints[i]) << std::dec << "\", "
                 << std::get<3>(data.matchedPoints[i]) << "]";
            if (i < data.matchedPoints.size() - 1) {
                file << ",";
            }
            file << "\n";
        }
        file << "  ]";
    }
    
    // Write JSON footer
    file << "\n}\n";
    
    file.close();
    std::cout << "Visualization data saved to: " << filename << std::endl;
    
    return true;
}

bool Visualizer::saveSessionsData(const std::vector<SessionData>& sessions, const std::string& filename) {
    // Create and open a JSON file to save the sessions data
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    
    // Write sessions as JSON array
    file << "[\n";
    for (size_t i = 0; i < sessions.size(); ++i) {
        const auto& session = sessions[i];
        file << "  {\n";
        file << "    \"id\": " << session.id << ",\n";
        file << "    \"matchCount\": " << session.matchCount << ",\n";
        file << "    \"confidence\": " << session.confidence << ",\n";
        file << "    \"mediaTitle\": \"" << session.mediaTitle << "\"\n";
        file << "  }";
        if (i < sessions.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    file << "]\n";
    
    file.close();
    std::cout << "Sessions data saved to: " << filename << std::endl;
    
    return true;
}

} // namespace afp 