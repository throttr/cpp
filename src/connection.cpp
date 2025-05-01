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
                                [this, self = shared_from_this(), handler = std::move(handler)](
                            boost::system::error_code ec, auto endpoints) mutable {
                                    if (ec) return handler(ec);

                                    boost::asio::async_connect(socket_, endpoints,
                                                               [self, handler = std::move(handler)](
                                                           boost::system::error_code ec, auto) mutable {
                                                                   handler(ec);
                                                               });
                                });
    }

    bool connection::is_open() const {
        return socket_.is_open();
    }

    void connection::send(std::vector<std::byte> buffer,
                          std::function<void(boost::system::error_code, std::vector<std::byte>)> handler) {
        auto self = shared_from_this(); // <== IMPORTANTE: preservar vivo antes de la lambda
        post(strand_, [self, buf = std::move(buffer), handler = std::move(handler)]() mutable {
            self->queue_.emplace_back(write_operation{std::move(buf), std::move(handler)});
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
                                                            [self, op_ptr](
                                                        boost::system::error_code ec,
                                                        std::size_t /*bytes_transferred*/) {
                                                                if (ec) {
                                                                    op_ptr->handler(ec, {});
                                                                    self->do_write();
                                                                    return;
                                                                }

                                                                std::size_t expected = 1;
                                                                const auto type = std::to_integer<uint8_t>(
                                                                    op_ptr->buffer_[0]);
                                                                if (type == 0x01 || type == 0x02) expected = 18;

                                                                auto recv_buf = std::make_shared<std::array<std::byte, 18>>();
                                                                boost::asio::async_read(
                                                                    self->socket_, boost::asio::buffer(*recv_buf),
                                                                    boost::asio::transfer_exactly(expected),
                                                                    boost::asio::bind_executor(self->strand_,
                                                                        [self, op_ptr, recv_buf, expected](boost::system::error_code ec2, std::size_t n) mutable {

                                                                            if (ec2) {
                                                                                op_ptr->handler(ec2, {});
                                                                            } else {
                                                                                std::vector<std::byte> response(recv_buf->begin(), recv_buf->begin() + n);
                                                                                op_ptr->handler({}, std::move(response));
                                                                            }

                                                                            self->do_write();
                                                                        }));
                                                            }));
    }
}
