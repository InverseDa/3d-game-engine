#ifndef VULKAN_LIB_UTILS_HPP
#define VULKAN_LIB_UTILS_HPP

#include <functional>

namespace ida {
// from: https://stackoverflow.com/a/57595105
template <typename T, typename... Rest>
void hashCombine(std::size_t& seed, const T& v, const Rest&... rest) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    (hashCombine(seed, rest), ...);
};

} // namespace ida

#endif // VULKAN_LIB_UTILS_HPP
