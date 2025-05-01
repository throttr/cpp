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
#include <boost/asio/experimental/channel.hpp>
#include <array>

namespace throttr {
    connection::connection(const boost::asio::any_io_executor & executor, std::string host, const uint16_t port)
        : strand_(make_strand(executor)),
          resolver_(strand_),
          socket_(strand_),
          host_(std::move(host)),
          port_(port) {
    }

    boost::asio::awaitable<void> connection::connect() {
        auto endpoints = co_await resolver_.async_resolve(host_, std::to_string(port_), boost::asio::use_awaitable);
        co_await async_connect(socket_, endpoints, boost::asio::use_awaitable);
    }

    bool connection::is_open() const {
        return socket_.is_open();
    }

    boost::asio::awaitable<void> connection::do_write() {

        while (true) {
            std::vector<std::byte> buffer;
            std::shared_ptr<boost::asio::experimental::channel<void(boost::system::error_code, std::vector<std::byte>)>> ch;

            {
                std::scoped_lock lock(mutex_);

                if (write_queue_.empty()) {
                    writing_ = false;
                    co_return;
                }

                buffer = std::move(write_queue_.front());
                write_queue_.pop_front();

                ch = std::move(pending_responses_.front());
                pending_responses_.pop_front();
            }

            co_await async_write(socket_, boost::asio::buffer(buffer), boost::asio::use_awaitable);

            std::array<std::byte, 18> recv_buf{};
            std::size_t expected_response_size = 1;
            const auto type = std::to_integer<uint8_t>(buffer[0]);
            if (type == 0x01 || type == 0x02) {
                expected_response_size = 18;
            }

            std::size_t n = co_await async_read(socket_, boost::asio::buffer(recv_buf),
                                                boost::asio::transfer_exactly(expected_response_size),
                                                boost::asio::use_awaitable);

            std::vector response(recv_buf.begin(), recv_buf.begin() + n);
            co_await ch->async_send({}, std::move(response));
        }

        co_return;
    }

    boost::asio::awaitable<std::vector<std::byte> > connection::send(std::vector<std::byte> buffer) {
        const auto ex = co_await boost::asio::this_coro::executor;
        auto ch = std::make_shared<boost::asio::experimental::channel<void(boost::system::error_code, std::vector<std::byte>)>>(ex, 1);

        bool need_write = false;

        {
            std::scoped_lock lock(mutex_);
            write_queue_.emplace_back(std::move(buffer));
            pending_responses_.emplace_back(ch);

            if (!writing_) {
                writing_ = true;
                need_write = true;
            }
        }

        if (need_write) {
            co_spawn(strand_, [this]() -> boost::asio::awaitable<void> {
                co_await do_write();
                co_return;
            }, boost::asio::detached);
        }

        auto [ec, response] = co_await ch->async_receive(boost::asio::as_tuple);
        if (ec) throw boost::system::system_error(ec);
        co_return response;
    }
}
