#include "fft_mkl.h"
#include <stdexcept>

namespace afp {

MKLFFT::~MKLFFT() {
    if (descriptor_) {
        DftiFreeDescriptor(&descriptor_);
    }
}

bool MKLFFT::init(size_t size) {
    size_ = size;
    
    // 创建 FFT 描述符
    MKL_LONG status = DftiCreateDescriptor(
        &descriptor_,
        DFTI_SINGLE,     // 单精度浮点数
        DFTI_REAL,       // 实数输入
        1,               // 一维
        size_           // 变换大小
    );
    if (status != DFTI_NO_ERROR) return false;

    // 设置输出格式为 COMPLEX
    status = DftiSetValue(descriptor_, DFTI_PACKED_FORMAT, DFTI_PACK_FORMAT);
    if (status != DFTI_NO_ERROR) return false;

    // 设置正向变换缩放因子
    float scale = 1.0f / size_;
    status = DftiSetValue(descriptor_, DFTI_FORWARD_SCALE, scale);
    if (status != DFTI_NO_ERROR) return false;

    // 提交描述符
    status = DftiCommitDescriptor(descriptor_);
    if (status != DFTI_NO_ERROR) return false;

    // 分配工作缓冲区
    work_buffer_.resize(size_ + 2);  // 额外空间用于复数输出
    
    return true;
}

bool MKLFFT::transform(const float* input, std::complex<float>* output) {
    // 复制输入数据到工作缓冲区
    std::copy(input, input + size_, work_buffer_.data());

    // 执行 FFT
    MKL_LONG status = DftiComputeForward(descriptor_, work_buffer_.data());
    if (status != DFTI_NO_ERROR) return false;

    // 转换 MKL 的打包格式到标准复数格式
    output[0] = std::complex<float>(work_buffer_[0], 0.0f);
    for (size_t i = 1; i < size_/2; ++i) {
        output[i] = std::complex<float>(
            work_buffer_[2*i-1],
            work_buffer_[2*i]
        );
    }
    output[size_/2] = std::complex<float>(work_buffer_[size_], 0.0f);

    return true;
}

} // namespace afp 