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

#ifndef THROTTR_WRITE_OPERATION_HPP
#define THROTTR_WRITE_OPERATION_HPP

#include <boost/system.hpp>
#include <future>
#include <vector>

namespace throttr
{
/**
 * Write
 */
struct write_operation
{
    /**
     * Buffer
     */
    std::vector<std::byte> buffer_;

    /**
     * Promise
     */
    std::function<void(boost::system::error_code, std::vector<std::byte>)> handler;
};
} // namespace throttr

#endif // THROTTR_WRITE_OPERATION_HPP
