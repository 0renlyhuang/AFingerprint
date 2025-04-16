#pragma once
#include "fft_interface.h"
#include <mkl.h>

namespace afp {

class MKLFFT : public FFTInterface {
public:
    ~MKLFFT() override;
    bool init(size_t size) override;
    bool transform(const float* input, std::complex<float>* output) override;

private:
    size_t size_ = 0;
    DFTI_DESCRIPTOR_HANDLE descriptor_ = nullptr;
    std::vector<float> work_buffer_;
};

} // namespace afp 