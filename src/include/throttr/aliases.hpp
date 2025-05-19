#ifndef THROTTR_ALIASES_HPP
#define THROTTR_ALIASES_HPP

#include <cstddef>
#include <vector>

namespace throttr {
    using vectorized_buffer = std::vector<std::byte>;
    using buffers = std::vector<vectorized_buffer>;
}

#endif // THROTTR_ALIASES_HPP
