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

#include <throttr/protocol_wrapper.hpp>
#include <throttr/exception.hpp>

#include <cstddef>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstddef>

namespace throttr {
    /**
     * Response query
     */
    struct response_query {
        /**
         * Success
         */
        bool success_ = false;

        /**
         * Quota remaining
         */
        value_type quota_ = 0;

        /**
         * TTL type
         */
        ttl_types ttl_type_ = ttl_types::milliseconds;

        /**
         * TTL remaining
         */
        value_type ttl_ = 0;

        /**
         * From buffer
         *
         * @param buffer
         * @return response_query
         */
        static response_query from_buffer(const std::vector<std::byte>& buffer) {
            if (buffer.size() != 18) {
                throw response_error("response_query: invalid buffer size");
            }

            response_query resp;

            resp.success_ = (buffer[0] == std::byte{0x01});

            std::memcpy(&resp.quota_, buffer.data() + 1, sizeof(resp.quota_));
            resp.ttl_type_ = static_cast<ttl_types>(std::to_integer<uint8_t>(buffer[9]));
            std::memcpy(&resp.ttl_, buffer.data() + 10, sizeof(resp.ttl_));

            return resp;
        }
    };

} // namespace throttr

#endif // THROTTR_RESPONSE_FULL_HPP
