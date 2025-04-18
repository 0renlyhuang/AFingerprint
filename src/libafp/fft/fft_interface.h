#pragma once
#include <vector>
#include <complex>

namespace afp {

class FFTInterface {
public:
    virtual ~FFTInterface() = default;
    virtual bool init(size_t size) = 0;
    virtual bool transform(const float* input, std::complex<float>* output) = 0;
};

class FFTFactory {
public:
    static std::unique_ptr<FFTInterface> create(size_t size);
};

} // namespace afp 