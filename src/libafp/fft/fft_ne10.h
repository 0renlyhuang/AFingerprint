#pragma once
#include "fft_interface.h"
#include <Ne10.h>

namespace afp {

class Ne10FFT : public FFTInterface {
public:
    ~Ne10FFT() override;
    bool init(size_t size) override;
    bool transform(const float* input, std::complex<float>* output) override;

private:
    size_t size_ = 0;
    ne10_fft_cfg_float32_t cfg_ = nullptr;
    std::vector<ne10_float32_t> buffer_;
};

} // namespace afp 