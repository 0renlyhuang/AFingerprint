#pragma once

#include <array>

namespace afp {

constexpr size_t max_channel_count = 2;

template<typename T>
using ChannelArray = std::array<T, max_channel_count>;

} // namespace afp