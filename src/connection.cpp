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
#include <array>
#include <iomanip>

namespace throttr {
    connection::connection(const boost::asio::any_io_executor &executor, std::string host, const uint16_t port)
        : strand_(make_strand(executor)),
          resolver_(strand_),
          socket_(strand_),
          host_(std::move(host)),
          port_(port) {
    }

    void connection::connect(std::function<void(boost::system::error_code)> handler) {
        resolver_.async_resolve(host_, std::to_string(port_),
                                [this, self = shared_from_this(), scope_handler = std::move(handler)](
                            const boost::system::error_code &ec, const auto& endpoints) mutable {
                                    if (ec) return scope_handler(ec);

                                    boost::asio::async_connect(socket_, endpoints,
                                                               [self, final_handler = std::move(scope_handler)](
                                                           const boost::system::error_code &connect_ec, auto) mutable {
                                                                   final_handler(connect_ec);
                                                               });
                                });
    }

    bool connection::is_open() const {
        return socket_.is_open();
    }

    void connection::send(std::vector<std::byte> buffer,
                          std::function<void(boost::system::error_code, std::vector<std::byte>)> handler) {
        auto self = shared_from_this();
        post(strand_, [self, buf = std::move(buffer), final_handler = std::move(handler)]() mutable {
            self->queue_.emplace_back(std::move(buf), std::move(final_handler));
            if (!self->writing_) {
                self->writing_ = true;
                self->do_write();
            }
        });
    }

    void connection::do_write() {
        if (queue_.empty()) {
            writing_ = false;
            return;
        }

        auto op_ptr = std::make_shared<write_operation>(std::move(queue_.front()));
        queue_.pop_front();

        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(op_ptr->buffer_),
            boost::asio::bind_executor(strand_,
                [self, op_ptr](const boost::system::error_code &ec, std::size_t /*bytes_transferred*/) {
                    if (ec) {
                        op_ptr->handler(ec, {});
                        self->do_write();
                        return;
                    }
                    self->handle_write(op_ptr);
                }));
    }

    void connection::handle_write(const std::shared_ptr<write_operation>& op) {
        std::size_t expected = 1;
        const auto type = std::to_integer<uint8_t>(op->buffer_[0]);
        if (type == 0x01 || type == 0x02) expected = 18;

        auto recv_buf = std::make_shared<std::array<std::byte, 18>>();
        auto self = shared_from_this();

        boost::asio::async_read(
            socket_, boost::asio::buffer(*recv_buf),
            boost::asio::transfer_exactly(expected),
            boost::asio::bind_executor(strand_,
                [self, op, recv_buf, expected](boost::system::error_code ec, std::size_t n) mutable {
                    if (ec) {
                        op->handler(ec, {});
                    } else {
                        std::vector<std::byte> response(recv_buf->begin(), recv_buf->begin() + n);
                        op->handler({}, std::move(response));
                    }
                    self->do_write();
                }));
    }
}
