#include "fft_interface.h"

#if defined(__APPLE__)
#include "fft_accelerate.h"
#elif defined(__ANDROID__)
#include "fft_ne10.h"
#else
#include "fft_kiss.h"
#endif

namespace afp {

std::unique_ptr<FFTInterface> FFTFactory::create(size_t size) {
#if defined(__APPLE__)
    auto fft = std::make_unique<AccelerateFFT>();
#elif defined(__ANDROID__)
    auto fft = std::make_unique<Ne10FFT>();
#else
    auto fft = std::make_unique<KissFFT>();
#endif

    if (!fft->init(size)) {
        return nullptr;
    }
    return fft;
}

} // namespace afp 