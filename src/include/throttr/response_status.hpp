// Copyright (C) 2025 Ian Torres
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#ifndef THROTTR_RESPONSE_STATUS_HPP
#define THROTTR_RESPONSE_STATUS_HPP

#include <cstddef>
#include <stdexcept>
#include <throttr/exception.hpp>
#include <vector>

namespace throttr {
/**
 * Response status
 */
struct response_status {
  /**
   * Success
   */
  bool success_ = false;

  /**
   * From buffer
   *
   * @param buffer
   * @return response_status
   */
  static response_status from_buffer(const std::vector<std::byte>& buffer) {
    if (buffer.size() != 1) {
      throw response_error("response_status: invalid buffer size");
    }

    return response_status{.success_ = (buffer[0] == std::byte{0x01})};
  }
};
}  // namespace throttr

#endif  // THROTTR_RESPONSE_STATUS_HPP
