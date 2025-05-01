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

#include <gtest/gtest.h>
#include <throttr/response_simple.hpp>
#include <throttr/response_full.hpp>
#include <stdexcept>

TEST(ResponseSimpleTest, ThrowsWhenBufferSizeIsInvalid) {
    const std::vector buffer(2, std::byte{0x01});

    try {
        throttr::response_simple::from_buffer(buffer);
        FAIL() << "Expected std::runtime_error due to invalid buffer size";
    } catch (const throttr::response_error& e) {
        EXPECT_STREQ(e.what(), "response_simple: invalid buffer size");
    }
}

TEST(ResponseSimpleTest, FromBufferSucceeds) {
    std::vector buffer(1, std::byte{0x01});
    auto [success] = throttr::response_simple::from_buffer(buffer);
    EXPECT_TRUE(success);
}

TEST(ResponseFullTest, ThrowsWhenBufferSizeIsInvalid) {
    const std::vector buffer(17, std::byte{0x01});

    try {
        throttr::response_full::from_buffer(buffer);
        FAIL() << "Expected std::runtime_error due to invalid buffer size";
    } catch (const throttr::response_error& e) {
        EXPECT_STREQ(e.what(), "response_full: invalid buffer size");
    }
}

TEST(ResponseFullTest, FromBufferSucceeds) {
    std::vector buffer(18, std::byte{0x01});

    buffer[0] = std::byte{0x01};  // success = true
    constexpr uint64_t quota_remaining = 100;
    std::memcpy(&buffer[1], &quota_remaining, sizeof(uint64_t)); // quota_remaining = 100
    buffer[9] = std::byte{0x01};  // ttl_type = milliseconds
    constexpr uint64_t ttl_remaining = 1000;
    std::memcpy(&buffer[10], &ttl_remaining, sizeof(int64_t)); // ttl_remaining = 1000

    const auto resp = throttr::response_full::from_buffer(buffer);

    EXPECT_TRUE(resp.success);
    EXPECT_EQ(resp.quota_remaining, 100);
    EXPECT_EQ(resp.ttl_type, throttr::ttl_types::milliseconds);
    EXPECT_EQ(resp.ttl_remaining, 1000);
}