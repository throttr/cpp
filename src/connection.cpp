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

#include <throttr/connection.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <array>

namespace throttr {
    connection::connection(boost::asio::any_io_executor executor, std::string host, const uint16_t port)
        : strand_(make_strand(executor)),
          resolver_(strand_),
          socket_(strand_),
          host_(std::move(host)),
          port_(port) {}

    boost::asio::awaitable<void> connection::connect() {
        auto endpoints = co_await resolver_.async_resolve(host_, std::to_string(port_), boost::asio::use_awaitable);
        co_await async_connect(socket_, endpoints, boost::asio::use_awaitable);
    }

    bool connection::is_open() const {
        return socket_.is_open();
    }

    boost::asio::awaitable<std::vector<std::byte>> connection::send(const std::vector<std::byte>& buffer) {
        co_await async_write(socket_, boost::asio::buffer(buffer), boost::asio::use_awaitable);

        std::array<std::byte, 18> recv_buf{};
        std::size_t n = co_await async_read(socket_, boost::asio::buffer(recv_buf), boost::asio::transfer_at_least(1), boost::asio::use_awaitable);

        std::vector response_data(recv_buf.begin(), recv_buf.begin() + n);
        co_return response_data;
    }
}

