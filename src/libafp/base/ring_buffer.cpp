#include "ring_buffer.h"
#include <stdexcept>
#include <algorithm>
#include "base/frame.h"
#include "base/fft_result.h"
#include <vector>

namespace afp {

template<typename T>
RingBuffer<T>::RingBuffer(size_t capacity)
    : buffer_(capacity)
    , write_pos_(0)
    , fill_count_(0) {
    if (capacity == 0) {
        throw std::invalid_argument("RingBuffer capacity must be greater than 0");
    }
}

template<typename T>
RingBuffer<T>::RingBuffer(RingBuffer&& other) noexcept
    : buffer_(std::move(other.buffer_))
    , write_pos_(other.write_pos_)
    , fill_count_(other.fill_count_) {
    other.write_pos_ = 0;
    other.fill_count_ = 0;
}

template<typename T>
RingBuffer<T>& RingBuffer<T>::operator=(RingBuffer&& other) noexcept {
    if (this != &other) {
        buffer_ = std::move(other.buffer_);
        write_pos_ = other.write_pos_;
        fill_count_ = other.fill_count_;
        other.write_pos_ = 0;
        other.fill_count_ = 0;
    }
    return *this;
}

template<typename T>
size_t RingBuffer<T>::write(const T* data, size_t count) {
    if (!data || count == 0) {
        return 0;
    }
    
    size_t available = availableSpace();
    size_t to_write = std::min(count, available);
    
    for (size_t i = 0; i < to_write; ++i) {
        buffer_[write_pos_] = data[i];
        write_pos_ = (write_pos_ + 1) % capacity();
        fill_count_++;
    }
    
    return to_write;
}

template<typename T>
bool RingBuffer<T>::push_back(const T& element) {
    if (full()) {
        return false; // 缓冲区已满
    }
    
    buffer_[write_pos_] = element;
    write_pos_ = (write_pos_ + 1) % capacity();
    fill_count_++;
    
    return true;
}

template<typename T>
size_t RingBuffer<T>::read(T* dest, size_t count) const {
    return readWithOffset(dest, count, 0);
}

template<typename T>
bool RingBuffer<T>::pop_front() {
    if (empty()) {
        return false; // 缓冲区为空
    }
    
    fill_count_--;
    // 不需要更新read position，因为getReadPos()会自动计算
    
    return true;
}

template<typename T>
size_t RingBuffer<T>::readWithOffset(T* dest, size_t count, size_t start_offset) const {
    if (!dest || count == 0 || start_offset >= fill_count_) {
        return 0;
    }
    
    size_t available = fill_count_ - start_offset;
    size_t to_read = std::min(count, available);
    
    size_t read_pos = getReadPos();
    read_pos = (read_pos + start_offset) % capacity();
    
    for (size_t i = 0; i < to_read; ++i) {
        dest[i] = buffer_[(read_pos + i) % capacity()];
    }
    
    return to_read;
}

template<typename T>
void RingBuffer<T>::moveWindow(size_t count) {
    if (count >= fill_count_) {
        // 如果要移除的数量大于等于当前数据量，清空缓冲区
        reset();
    } else {
        fill_count_ -= count;
        // write_pos_ 不需要改变，因为它总是指向下一个写入位置
    }
}

template<typename T>
void RingBuffer<T>::reset() {
    write_pos_ = 0;
    fill_count_ = 0;
}

template<typename T>
T& RingBuffer<T>::operator[](size_t index) {
    if (index >= fill_count_) {
        throw std::out_of_range("RingBuffer index out of range");
    }
    
    size_t read_pos = getReadPos();
    size_t actual_pos = (read_pos + index) % capacity();
    return buffer_[actual_pos];
}

template<typename T>
const T& RingBuffer<T>::operator[](size_t index) const {
    if (index >= fill_count_) {
        throw std::out_of_range("RingBuffer index out of range");
    }
    
    size_t read_pos = getReadPos();
    size_t actual_pos = (read_pos + index) % capacity();
    return buffer_[actual_pos];
}

template<typename T>
size_t RingBuffer<T>::getReadPos() const {
    if (fill_count_ == 0) {
        return write_pos_;
    }
    return (write_pos_ - fill_count_ + capacity()) % capacity();
}

template<typename T>
const T& RingBuffer<T>::back() const {
    if (fill_count_ == 0) {
        throw std::runtime_error("RingBuffer is empty");
    }
    size_t last_pos = (write_pos_ - 1 + capacity()) % capacity();
    return buffer_[last_pos];
}

template<typename T>
const T& RingBuffer<T>::front() const {
    if (fill_count_ == 0) {
        throw std::runtime_error("RingBuffer is empty");
    }
    size_t read_pos = getReadPos();
    return buffer_[read_pos];
}

template<typename T>
std::vector<T> RingBuffer<T>::getRange(size_t start_index, size_t count) const {
    if (start_index >= fill_count_) {
        return {};
    }
    
    size_t available = fill_count_ - start_index;
    size_t to_read = std::min(count, available);
    
    std::vector<T> result;
    result.reserve(to_read);
    
    size_t read_pos = getReadPos();
    read_pos = (read_pos + start_index) % capacity();
    
    for (size_t i = 0; i < to_read; ++i) {
        result.push_back(buffer_[(read_pos + i) % capacity()]);
    }
    
    return result;
}

// 显式实例化常用类型
template class RingBuffer<float>;
template class RingBuffer<double>;
template class RingBuffer<int>;
template class RingBuffer<short>;
template class RingBuffer<char>;

// 添加Frame类型的包含和实例化
template class RingBuffer<afp::Frame>;

// 添加FFTResult类型的包含和实例化  
template class RingBuffer<afp::FFTResult>;

} // namespace afp 