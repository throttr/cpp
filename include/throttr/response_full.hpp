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

#ifndef THROTTR_RESPONSE_FULL_HPP
#define THROTTR_RESPONSE_FULL_HPP

#include <throttr/protocol.hpp>

#include <cstddef>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstddef>

namespace throttr {
    /**
     * Response full
     */
    struct response_full {
        /**
         * Success
         */
        bool success = false;

        /**
         * Quota remaining
         */
        uint64_t quota_remaining = 0;

        /**
         * TTL type
         */
        ttl_types ttl_type = ttl_types::milliseconds;

        /**
         * TTL remaining
         */
        int64_t ttl_remaining = 0;

        /**
         * From buffer
         *
         * @param buffer
         * @return response_full
         */
        static response_full from_buffer(const std::vector<std::byte>& buffer) {
            if (buffer.size() != 18) {
                throw std::runtime_error("response_full: invalid buffer size");
            }

            response_full resp;

            resp.success = (buffer[0] == std::byte{0x01});

            std::memcpy(&resp.quota_remaining, buffer.data() + 1, sizeof(resp.quota_remaining));
            resp.ttl_type = static_cast<ttl_types>(std::to_integer<uint8_t>(buffer[9]));
            std::memcpy(&resp.ttl_remaining, buffer.data() + 10, sizeof(resp.ttl_remaining));

            return resp;
        }
    };

} // namespace throttr

#endif // THROTTR_RESPONSE_FULL_HPP
