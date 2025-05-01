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
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "response_simple: invalid buffer size");
    } catch (...) {
        FAIL() << "Expected std::runtime_error, but got a different exception";
    }
}

TEST(ResponseFullTest, ThrowsWhenBufferSizeIsInvalid) {
    const std::vector buffer(17, std::byte{0x01});

    try {
        throttr::response_full::from_buffer(buffer);
        FAIL() << "Expected std::runtime_error due to invalid buffer size";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "response_full: invalid buffer size");
    } catch (...) {
        FAIL() << "Expected std::runtime_error, but got a different exception";
    }
}