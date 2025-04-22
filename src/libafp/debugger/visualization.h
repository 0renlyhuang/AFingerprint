#pragma once
#include <vector>
#include <string>
#include <map>
#include <memory>

namespace afp {

// Structure to hold data for visualization
struct VisualizationData {
    // Stores all peaks detected in the audio (frequency, timestamp)
    std::vector<std::pair<uint32_t, double>> allPeaks;
    
    // Stores the selected fingerprint points (frequency, timestamp, hash)
    std::vector<std::tuple<uint32_t, double, uint32_t>> fingerprintPoints;
    
    // Stores matched points during matching (frequency, timestamp, hash, session_id)
    std::vector<std::tuple<uint32_t, double, uint32_t, uint32_t>> matchedPoints;
    
    // Metadata
    std::string title;
    double duration;
};

// Structure to store top matching sessions for visualization
struct SessionData {
    uint32_t id;
    uint32_t matchCount;
    double confidence;
    std::string mediaTitle;
};

class Visualizer {
public:
    Visualizer();
    ~Visualizer();
    
    // Save visualization data to a JSON file (no Python script generation)
    static bool saveVisualization(const VisualizationData& data, const std::string& filename);
    
    // Save top matching sessions data to a JSON file
    static bool saveSessionsData(const std::vector<SessionData>& sessions, const std::string& filename);
    
    // Get singleton instance
    static Visualizer& getInstance();
    
private:
    // Visualization data storage
    std::map<std::string, VisualizationData> dataStore_;
};

} // namespace afp 
