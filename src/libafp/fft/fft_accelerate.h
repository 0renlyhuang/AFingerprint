#pragma once
#include "fft_interface.h"
#include <Accelerate/Accelerate.h>

namespace afp {

class AccelerateFFT : public FFTInterface {
public:
    ~AccelerateFFT() override;
    bool init(size_t size) override;
    bool transform(const float* input, std::complex<float>* output) override;

private:
    size_t size_ = 0;
    size_t log2n_ = 0;
    FFTSetup fft_setup_ = nullptr;
    std::vector<float> split_real_;
    std::vector<float> split_imag_;
    DSPSplitComplex split_complex_;
};

} // namespace afp 