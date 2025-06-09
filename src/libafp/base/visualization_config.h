#pragma once

#include "debugger/visualization.h"

namespace afp {

struct VisualizationConfig {
    bool collectVisualizationData_ = false;
    VisualizationData visualizationData_;
};

}