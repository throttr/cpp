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
#include <iostream>

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
            // LCOV_EXCL_START
            if (!self->writing_) {
                // LCOV_EXCL_STOP
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

        auto op = std::make_shared<write_operation>(std::move(queue_.front()));
        queue_.pop_front();

        auto self = shared_from_this();
        boost::asio::async_write(socket_, boost::asio::buffer(op->buffer_),
            boost::asio::bind_executor(strand_,
                [self, op](const boost::system::error_code &ec, std::size_t /*bytes_transferred*/) {
                    // LCOV_EXCL_START
                    if (ec) {
                        op->handler(ec, {});
                        self->do_write();
                        return;
                    }
                    // LCOV_EXCL_STOP
                    self->handle_write(op);
                }));
    }

    void connection::handle_write(const std::shared_ptr<write_operation>& op) {
        auto self = shared_from_this();
        if (const auto type = std::to_integer<uint8_t>(op->buffer_[0]); type == 0x02 || type == 0x06) {
            auto head = std::make_shared<std::array<std::byte, 1>>();
            boost::asio::async_read(
                socket_, boost::asio::buffer(*head),
                boost::asio::transfer_exactly(1),
                boost::asio::bind_executor(strand_,
                    [self, op, head](const boost::system::error_code& ec, std::size_t) {
                        if (ec) {
                            op->handler(ec, {});
                            self->do_write();
                            return;
                        }

                        const auto status = std::to_integer<uint8_t>((*head)[0]);
                        if (status == 0x00) {
                            // Error, no hay cuerpo adicional
                            op->handler({}, std::vector(head->begin(), head->end()));
                            self->do_write();
                            return;
                        }

                        constexpr std::size_t value_payload = sizeof(uint16_t) * 2 + 1;
                        auto rest = std::make_shared<std::vector<std::byte>>(value_payload);
                        boost::asio::async_read(
                            self->socket_, boost::asio::buffer(*rest),
                            boost::asio::transfer_exactly(value_payload),
                            boost::asio::bind_executor(self->strand_,
                                [self, op, head, rest](const boost::system::error_code& ec, std::size_t) {
                                    if (ec) {
                                        op->handler(ec, {});
                                    } else {
                                        // Unir head + rest
                                        std::vector<std::byte> full;
                                        full.reserve(1 + rest->size());
                                        full.insert(full.end(), head->begin(), head->end());
                                        full.insert(full.end(), rest->begin(), rest->end());
                                        op->handler({}, std::move(full));
                                    }
                                    self->do_write();
                                }));
                    }));
        } else {
            auto response_buffer = std::make_shared<std::vector<std::byte>>(1);
            boost::asio::async_read(
                socket_, boost::asio::buffer(*response_buffer),
                boost::asio::transfer_exactly(1),
                boost::asio::bind_executor(strand_,
                    [self, op, response_buffer](const boost::system::error_code& ec, std::size_t) {
                        op->handler(ec, ec ? std::vector<std::byte>{} : std::move(*response_buffer));
                        self->do_write();
                    }));
        }
    }
}
