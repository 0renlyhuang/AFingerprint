#include "fft_accelerate.h"
#include <cmath>

namespace afp {

AccelerateFFT::~AccelerateFFT() {
    if (fft_setup_) {
        vDSP_destroy_fftsetup(fft_setup_);
    }
}

bool AccelerateFFT::init(size_t size) {
    size_ = size;
    log2n_ = static_cast<size_t>(std::log2(size));
    
    // 创建 FFT 设置
    fft_setup_ = vDSP_create_fftsetup(log2n_, FFT_RADIX2);
    if (!fft_setup_) return false;
    
    // 分配分离复数缓冲区
    split_real_.resize(size_);
    split_imag_.resize(size_);
    split_complex_.realp = split_real_.data();
    split_complex_.imagp = split_imag_.data();
    
    return true;
}

bool AccelerateFFT::transform(const float* input, std::complex<float>* output) {
    // 将实数输入转换为分离复数格式
    vDSP_ctoz((DSPComplex*)input, 2, &split_complex_, 1, size_/2);
    
    // 执行 FFT
    vDSP_fft_zrip(fft_setup_, &split_complex_, 1, log2n_, FFT_FORWARD);
    
    // 缩放结果
    float scale = 1.0f / (2 * size_);
    vDSP_vsmul(split_complex_.realp, 1, &scale, split_complex_.realp, 1, size_/2);
    vDSP_vsmul(split_complex_.imagp, 1, &scale, split_complex_.imagp, 1, size_/2);
    
    // 转换回交错复数格式
    for (size_t i = 0; i < size_/2; ++i) {
        output[i] = std::complex<float>(split_complex_.realp[i], split_complex_.imagp[i]);
    }
    
    return true;
}

} // namespace afp 