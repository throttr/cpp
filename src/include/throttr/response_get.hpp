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

#ifndef THROTTR_RESPONSE_GET_HPP
#define THROTTR_RESPONSE_GET_HPP

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <throttr/exception.hpp>
#include <throttr/protocol_wrapper.hpp>
#include <vector>

namespace throttr {
/**
 * Response GET
 */
struct response_get {
  /**
   * Success
   */
  bool success_ = false;

  /**
   * TTL type
   */
  ttl_types ttl_type_ = ttl_types::milliseconds;

  /**
   * TTL remaining
   */
  value_type ttl_ = 0;

  std::vector<std::byte> value_;

  /**
   * From buffer
   *
   * @param buffer
   * @return response_query
   */
  static response_get from_buffer(const std::vector<std::byte>& buffer) {
    if (buffer.size() == 1 || buffer.size() > sizeof(value_type) * 2 + 1) {
      response_get _resp;

      _resp.success_ = (buffer[0] == std::byte{0x01});

      if (buffer.size() == 1) {
        return _resp;
      }
      constexpr std::size_t N = sizeof(value_type);
      std::size_t offset = 1;

      // TTL Type (1 byte)
      _resp.ttl_type_ =
          static_cast<ttl_types>(std::to_integer<uint8_t>(buffer[offset]));
      offset += 1;

      // TTL (N bytes)
      std::memcpy(&_resp.ttl_, buffer.data() + offset, N);
      offset += N;

      // Size (N bytes)
      value_type size = 0;
      std::memcpy(&size, buffer.data() + offset, N);
      offset += N;

      if (buffer.size() != offset + size)
        throw response_error(
            "response_get: buffer size mismatch with value length");

      // Value (M bytes)
      _resp.value_.resize(size);
      std::memcpy(_resp.value_.data(), buffer.data() + offset, size);

      return _resp;
    }
    throw response_error("response_get: invalid buffer size");
  }
};

}  // namespace throttr

#endif  // THROTTR_RESPONSE_GET_HPP
