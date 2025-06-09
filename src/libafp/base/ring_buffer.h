#pragma once

#include <vector>
#include <cstddef>

namespace afp {

template<typename T>
class RingBuffer {
public:
    // 构造函数
    explicit RingBuffer(size_t capacity);
    
    // 析构函数
    ~RingBuffer() = default;
    
    // 禁用拷贝构造和赋值
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    // 移动构造和赋值
    RingBuffer(RingBuffer&& other) noexcept;
    RingBuffer& operator=(RingBuffer&& other) noexcept;
    
    // 写入数据到缓冲区
    // 返回实际写入的元素数量
    size_t write(const T* data, size_t count);
    
    // 添加单个元素到缓冲区末尾
    bool push_back(const T& element);
    
    // 从缓冲区读取数据到目标数组
    // 返回实际读取的元素数量
    size_t read(T* dest, size_t count) const;
    
    // 从缓冲区前端移除一个元素
    bool pop_front();
    
    // 从缓冲区读取数据到目标数组，支持环形读取
    // start_offset: 相对于最早数据的偏移量
    size_t readWithOffset(T* dest, size_t count, size_t start_offset = 0) const;
    
    // 移动窗口，移除指定数量的元素
    void moveWindow(size_t count);
    
    // 获取当前缓冲区中的元素数量
    size_t size() const { return fill_count_; }
    
    // 获取缓冲区的总容量
    size_t capacity() const { return buffer_.size(); }
    
    // 获取可用空间
    size_t availableSpace() const { return capacity() - size(); }
    
    // 检查缓冲区是否为空
    bool empty() const { return fill_count_ == 0; }
    
    // 检查缓冲区是否已满
    bool full() const { return fill_count_ == capacity(); }
    
    // 重置缓冲区
    void reset();
    
    // 获取指定位置的元素引用（相对于最早的数据）
    T& operator[](size_t index);
    const T& operator[](size_t index) const;
    
    // 获取最新添加的元素
    const T& back() const;
    
    // 获取最早的元素
    const T& front() const;
    
    // 获取指定范围内的元素
    std::vector<T> getRange(size_t start_index, size_t count) const;
    
    // 检查是否有足够的元素用于窗口操作
    bool hasMinimumElements(size_t min_count) const { return fill_count_ >= min_count; }

    // 兼容性别名
    bool push(const T& element) { return push_back(element); }
    bool pop() { return pop_front(); }

private:
    std::vector<T> buffer_;
    size_t write_pos_;
    size_t fill_count_;
    
    // 计算读取位置
    size_t getReadPos() const;
};

template<typename T>
using RingBufferPtr = std::unique_ptr<RingBuffer<T>>;

} // namespace afp 