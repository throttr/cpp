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
#include <throttr/response_status.hpp>
#include <throttr/response_query.hpp>
#include <throttr/response_get.hpp>
#include <stdexcept>

TEST(ResponseStatusTest, ThrowsWhenBufferSizeIsInvalid) {
    const std::vector _buffer(2, std::byte{0x01});

    try {
        throttr::response_status::from_buffer(_buffer);
        FAIL() << "Expected std::runtime_error due to invalid buffer size";
    } catch (const throttr::response_error& error) {
        EXPECT_STREQ(error.what(), "response_status: invalid buffer size");
    }
}

TEST(ResponseStatusTest, FromBufferSucceeds) {
    const std::vector _buffer(1, std::byte{0x01});
    auto [_success] = throttr::response_status::from_buffer(_buffer);
    EXPECT_TRUE(_success);
}

TEST(ResponseQueryTest, ThrowsWhenBufferSizeIsInvalid) {
    const std::vector _buffer(17, std::byte{0x01});

    try {
        throttr::response_query::from_buffer(_buffer);
        FAIL() << "Expected std::runtime_error due to invalid buffer size";
    } catch (const throttr::response_error& error) {
        EXPECT_STREQ(error.what(), "response_query: invalid buffer size");
    }
}

TEST(ResponseQueryTest, FromBufferSucceeds) {
    constexpr int _expected_buffer_size = sizeof(throttr::value_type) * 2 + 2;
    std::vector _buffer(_expected_buffer_size, std::byte{0x01});

    _buffer[0] = std::byte{0x01};  // success = true
    constexpr throttr::value_type _quota = 7;
    std::memcpy(&_buffer[1], &_quota, sizeof(throttr::value_type)); // quota = 7
    _buffer[sizeof(throttr::value_type) + 1] = std::byte{0x03};  // ttl_type = milliseconds
    constexpr throttr::value_type _ttl = 3;
    std::memcpy(&_buffer[sizeof(throttr::value_type) * 2], &_ttl, sizeof(throttr::value_type)); // ttl = 3

    const auto _resp = throttr::response_query::from_buffer(_buffer);

    EXPECT_TRUE(_resp.success_);
    EXPECT_EQ(_resp.quota_, 7);
    EXPECT_EQ(_resp.ttl_type_, throttr::ttl_types::milliseconds);
    EXPECT_EQ(_resp.ttl_, 3);
}

TEST(ResponseGetTest, FromBufferSuccessOnlyByte01) {
    const std::vector buffer = {std::byte{0x01}};
    const auto resp = throttr::response_get::from_buffer(buffer);
    EXPECT_TRUE(resp.success_);
    EXPECT_TRUE(resp.value_.empty());
}

TEST(ResponseGetTest, FromBufferSuccessOnlyByte00) {
    const std::vector buffer = {std::byte{0x00}};
    const auto resp = throttr::response_get::from_buffer(buffer);
    EXPECT_FALSE(resp.success_);
    EXPECT_TRUE(resp.value_.empty());
}

TEST(ResponseGetTest, ThrowsWhenMetadataIncomplete) {
    const std::vector buffer = {std::byte{0x01}, std::byte{0x03}};
    try {
        throttr::response_get::from_buffer(buffer);
        FAIL() << "Expected response_error due to short metadata";
    } catch (const throttr::response_error& error) {
        EXPECT_STREQ(error.what(), "response_get: invalid buffer size");
    }
}

TEST(ResponseGetTest, ThrowsWhenValueSizeMismatch) {
    constexpr std::size_t N = sizeof(throttr::value_type);
    std::vector buffer(1 + 1 + N + N + 2, std::byte{0x01}); // intentionally longer

    buffer[0] = std::byte{0x01}; // success
    buffer[1] = std::byte{0x03}; // ttl_type
    throttr::value_type ttl = 5;
    std::memcpy(buffer.data() + 2, &ttl, N);
    throttr::value_type wrong_size = 1;
    std::memcpy(buffer.data() + 2 + N, &wrong_size, N);

    try {
        throttr::response_get::from_buffer(buffer);
        FAIL() << "Expected response_error due to size mismatch";
    } catch (const throttr::response_error& error) {
        EXPECT_STREQ(error.what(), "response_get: buffer size mismatch with value length");
    }
}

TEST(ResponseGetTest, FromBufferSuccessFull) {
    constexpr std::size_t N = sizeof(throttr::value_type);
    constexpr throttr::value_type ttl = 42;
    constexpr throttr::value_type size = 3;
    const std::vector<std::byte> value = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}};

    std::vector<std::byte> buffer(1 + 1 + N + N + size);
    std::size_t offset = 0;
    buffer[offset++] = std::byte{0x01}; // success
    buffer[offset++] = std::byte{0x03}; // ttl_type
    std::memcpy(buffer.data() + offset, &ttl, N);
    offset += N;
    std::memcpy(buffer.data() + offset, &size, N);
    offset += N;
    std::memcpy(buffer.data() + offset, value.data(), size);

    const auto resp = throttr::response_get::from_buffer(buffer);
    EXPECT_TRUE(resp.success_);
    EXPECT_EQ(resp.ttl_type_, throttr::ttl_types::milliseconds);
    EXPECT_EQ(resp.ttl_, ttl);
    EXPECT_EQ(resp.value_, value);
}