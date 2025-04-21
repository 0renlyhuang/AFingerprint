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
    
    // Stores matched points during matching (frequency, timestamp, hash)
    std::vector<std::tuple<uint32_t, double, uint32_t>> matchedPoints;
    
    // Metadata
    std::string title;
    double duration;
};

class Visualizer {
public:
    Visualizer();
    ~Visualizer();
    
    // Save visualization data to a file
    static bool saveVisualization(const VisualizationData& data, const std::string& filename);
    
    // Generate fingerprint extraction visualization
    static bool generateExtractionPlot(const VisualizationData& data, const std::string& filename);
    
    // Generate matching visualization
    static bool generateMatchingPlot(const VisualizationData& sourceData, 
                                   const VisualizationData& queryData,
                                   const std::string& filename);
    
    // Get singleton instance
    static Visualizer& getInstance();
    
private:
    // Visualization data storage
    std::map<std::string, VisualizationData> dataStore_;
};

} // namespace afp 
