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

#ifndef THROTTR_CONNECTION_HPP
#define THROTTR_CONNECTION_HPP

#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <throttr/aliases.hpp>
#include <throttr/protocol_wrapper.hpp>
#include <throttr/write_operation.hpp>
#include <vector>

namespace throttr {
/**
 * Connection
 */
class connection : public std::enable_shared_from_this<connection> {
 public:
  /**
   * Constructor
   *
   * @param executor
   * @param host
   * @param port
   */
  connection(const boost::asio::any_io_executor& executor,
             std::string host,
             uint16_t port)
      : strand_(make_strand(executor)),
        resolver_(strand_),
        socket_(strand_),
        host_(std::move(host)),
        port_(port) {}

  /**
   * Connect
   *
   * @param handler
   */
  void connect(std::function<void(boost::system::error_code)> handler) {
    resolver_.async_resolve(
        host_, std::to_string(port_),
        [this, self = shared_from_this(), _scope_handler = std::move(handler)](
            const boost::system::error_code& ec,
            const auto& endpoints) mutable {
          // LCOV_EXCL_START
          if (ec)
            return _scope_handler(ec);
          // LCOV_EXCL_STOP

          boost::asio::async_connect(
              socket_, endpoints,
              [self, _final_handler = std::move(_scope_handler)](
                  const boost::system::error_code& connect_ec, auto) mutable {
                _final_handler(connect_ec);
              });
        });
  };

  /**
   * Send
   *
   * @param buffer
   * @param handler
   */
  void send(std::vector<std::byte> buffer,
            std::function<void(boost::system::error_code,
                               std::vector<std::vector<std::byte> >)> handler) {
    auto _self = shared_from_this();
    std::vector _heads = {std::byte{buffer[0]}};
    post(strand_, [_self, _scoped_buffer = std::move(buffer),
                   _scoped_heads = std::move(_heads),
                   _final_handler = std::move(handler)]() mutable {
      _self->queue_.emplace_back(std::move(_scoped_buffer),
                                 std::move(_scoped_heads),
                                 std::move(_final_handler));
      // LCOV_EXCL_START
      if (!_self->writing_) {
        // LCOV_EXCL_STOP
        _self->writing_ = true;
        _self->do_write();
      }
    });
  }

  void sendMany(
      std::vector<std::vector<std::byte> > chunks,
      std::function<void(boost::system::error_code,
                         std::vector<std::vector<std::byte> >)> handler) {
    std::vector<std::byte> _heads;
    std::vector<std::byte> _buffer;
    _heads.reserve(chunks.size());
    size_t _total_size = 0;
    for (const auto& buffer : chunks) {
      _heads.push_back(buffer.at(0));
      _total_size += buffer.size();
    }

    _buffer.reserve(_total_size);

    for (const auto& chunk : chunks) {
      _buffer.insert(_buffer.end(), chunk.begin(), chunk.end());
    }

    auto _self = shared_from_this();
    post(strand_, [_self, _scoped_buffer = std::move(_buffer),
                   _scoped_heads = std::move(_heads),
                   _final_handler = std::move(handler)]() mutable {
      _self->queue_.emplace_back(std::move(_scoped_buffer),
                                 std::move(_scoped_heads),
                                 std::move(_final_handler));
      // LCOV_EXCL_START
      if (!_self->writing_) {
        // LCOV_EXCL_STOP
        _self->writing_ = true;
        _self->do_write();
      }
    });
  }

  /**
   * Is open
   *
   * @return bool
   */
  [[nodiscard]] bool is_open() const { return socket_.is_open(); }

 private:
  /**
   * Do write
   */
  void do_write() {
    // LCOV_EXCL_START
    if (queue_.empty()) {
      // LCOV_EXCL_STOP
      writing_ = false;
      return;
    }

    auto _operation =
        std::make_shared<write_operation>(std::move(queue_.front()));
    queue_.pop_front();

    auto _self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(_operation->buffer_),
        boost::asio::bind_executor(
            strand_, [_self, _operation](const boost::system::error_code& ec,
                                         std::size_t /*bytes_transferred*/) {
              // LCOV_EXCL_START
              if (ec) {
                _operation->handler(ec, {});
                _self->do_write();
                return;
              }
              // LCOV_EXCL_STOP
              _self->handle_write(_operation);
            }));
  }

  /**
   * Handle
   * @param operation
   */
  void handle_write(const std::shared_ptr<write_operation>& operation) {
    if (operation->heads_.empty()) {
      operation->handler({}, std::move(operation->responses_));
      do_write();
      return;
    }

    const auto _type = std::to_integer<uint8_t>(operation->heads_.front());
    operation->heads_.erase(operation->heads_.begin());

    auto _self = shared_from_this();

    auto _continuation = [_self, operation](boost::system::error_code ec,
                                            std::vector<std::byte> response) {
      if (ec) {
        operation->handler(ec, {});
        _self->do_write();
        return;
      }

      operation->responses_.emplace_back(std::move(response));
      _self->handle_write(operation);
    };

    switch (_type) {
      case 0x02: {
        const auto _head = std::make_shared<std::array<std::byte, 1> >();
        (*_head)[0] = std::byte{_type};
        read_query_value(operation, _head, std::move(_continuation));
        break;
      }
      case 0x06: {
        const auto _head = std::make_shared<std::array<std::byte, 1> >();
        (*_head)[0] = std::byte{_type};
        read_get_header(operation, _head, std::move(_continuation));
        break;
      }
      default:
        read_status_value(operation, std::move(_continuation));
        break;
    }
  }

  void read_status_value(const std::shared_ptr<write_operation>& op,
                         std::function<void(boost::system::error_code,
                                            std::vector<std::byte>)> next) {
    auto _self = shared_from_this();
    auto _buf = std::make_shared<std::vector<std::byte> >(1);

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_buf), boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_,
            [_self, op, _buf, next](boost::system::error_code ec, std::size_t) {
              if (ec) {
                next(ec, {});
              } else {
                next({}, std::move(*_buf));
              }
            }));
  }

  /**
   * Read response head
   *
   * @param operation
   * @param on_success
   */
  void read_response_head(
      const std::shared_ptr<write_operation>& operation,
      const std::function<void(std::shared_ptr<std::array<std::byte, 1> >)>&
          on_success) {
    auto _self = shared_from_this();
    auto _head = std::make_shared<std::array<std::byte, 1> >();

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_head), boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [_self, operation, _head, on_success](
                         const boost::system::error_code& ec, std::size_t) {
              // LCOV_EXCL_START
              if (ec) {
                operation->handler(ec, {});
                _self->do_write();
                return;
              }
              // LCOV_EXCL_STOP

              if (const auto _status = std::to_integer<uint8_t>((*_head)[0]);
                  _status == 0x00) {
                // ✅ En lugar de llamar directamente al handler:
                std::vector<std::byte> _full(_head->begin(), _head->end());
                operation->responses_.emplace_back(std::move(_full));
                _self->handle_write(operation);
                return;
              }

              on_success(_head);
            }));
  }

  /**
   * Read GET header
   *
   * @param operation
   * @param head
   */
  void read_get_header(const std::shared_ptr<write_operation>& operation,
                       const std::shared_ptr<std::array<std::byte, 1> >& head,
                       std::function<void(boost::system::error_code,
                                          std::vector<std::byte>)> next) {
    auto _self = shared_from_this();
    auto _success = std::make_shared<std::array<std::byte, 1> >();
    boost::asio::async_read(
        socket_, boost::asio::buffer(*_success),
        boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [_self, operation, head, _success, next](
                         const boost::system::error_code& ec, std::size_t) {
              if (ec) {
                next(ec, {});
                return;
              }

              if ((*_success)[0] == std::byte{0x00}) {
                std::vector<std::byte> _result;
                _result.push_back((*head)[0]);
                _result.push_back((*_success)[0]);
                next({}, std::move(_result));
                return;
              }

              constexpr std::size_t N = sizeof(value_type);
              constexpr std::size_t _header_size = 1 + N + N;

              auto _header =
                  std::make_shared<std::vector<std::byte> >(_header_size);
              boost::asio::async_read(
                  _self->socket_, boost::asio::buffer(*_header),
                  boost::asio::transfer_exactly(_header_size),
                  boost::asio::bind_executor(
                      _self->strand_,
                      [_self, operation, head, _success, _header, next](
                          const boost::system::error_code& ec2, std::size_t) {
                        if (ec2) {
                          next(ec2, {});
                          return;
                        }

                        value_type _value_size = 0;
                        std::memcpy(&_value_size,
                                    _header->data() + 1 + sizeof(value_type),
                                    sizeof(value_type));

                        auto _value = std::make_shared<std::vector<std::byte> >(
                            _value_size);
                        boost::asio::async_read(
                            _self->socket_, boost::asio::buffer(*_value),
                            boost::asio::transfer_exactly(_value_size),
                            boost::asio::bind_executor(
                                _self->strand_,
                                [head, _success, _header, _value, next](
                                    const boost::system::error_code& ec3,
                                    std::size_t) {
                                  if (ec3) {
                                    next(ec3, {});
                                    return;
                                  }

                                  std::vector<std::byte> _full;
                                  _full.reserve(1 + 1 + _header->size() +
                                                _value->size());
                                  _full.push_back((*_success)[0]);
                                  _full.insert(_full.end(), _header->begin(),
                                               _header->end());
                                  _full.insert(_full.end(), _value->begin(),
                                               _value->end());
                                  next({}, std::move(_full));
                                }));
                      }));
            }));
  }

  /**
   * Read GET value
   *
   * @param operation
   * @param head
   * @param header
   * @param size
   */
  void read_get_value(const std::shared_ptr<write_operation>& operation,
                      const std::shared_ptr<std::array<std::byte, 1> >& head,
                      const std::shared_ptr<std::vector<std::byte> >& header,
                      value_type size,
                      std::function<void(boost::system::error_code,
                                         std::vector<std::byte>)> next) {
    auto _self = shared_from_this();
    auto _value = std::make_shared<std::vector<std::byte> >(size);

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_value),
        boost::asio::transfer_exactly(size),
        boost::asio::bind_executor(
            strand_, [_self, operation, head, header, _value, next](
                         const boost::system::error_code& ec, std::size_t) {
              if (ec) {
                // LCOV_EXCL_START
                next(ec, {});
                return;
                // LCOV_EXCL_STOP
              }
              std::vector<std::byte> _full;
              _full.reserve(1 + header->size() + _value->size());
              _full.insert(_full.end(), head->begin(), head->end());
              _full.insert(_full.end(), header->begin(), header->end());
              _full.insert(_full.end(), _value->begin(), _value->end());
              next({}, std::move(_full));
            }));
  }

  /**
   * Read query value
   *
   * @param operation
   * @param head
   * @param next
   */
  void read_query_value(const std::shared_ptr<write_operation>& operation,
                        const std::shared_ptr<std::array<std::byte, 1> >& head,
                        std::function<void(boost::system::error_code,
                                           std::vector<std::byte>)> next) {
    constexpr std::size_t payload_size = sizeof(value_type) * 2 + 1 + 1;

    auto _self = shared_from_this();
    auto _rest = std::make_shared<std::vector<std::byte> >(payload_size);

    auto success_byte = std::make_shared<std::array<std::byte, 1> >();

    boost::asio::async_read(
        socket_, boost::asio::buffer(*success_byte),
        boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [_self, operation, success_byte, next](
                         const boost::system::error_code& ec, std::size_t) {
              if (ec) {
                next(ec, {});
                return;
              }

              // Si success == 0x00, devolvemos solo ese byte
              if ((*success_byte)[0] == std::byte{0x00}) {
                next({}, {success_byte->begin(), success_byte->end()});
                return;
              }

              // Si success == 0x01, seguimos leyendo: quota (4), ttl_type (1),
              // ttl (4)
              constexpr std::size_t rest_size = sizeof(value_type) * 2 + 1;
              auto rest = std::make_shared<std::vector<std::byte> >(rest_size);

              boost::asio::async_read(
                  _self->socket_, boost::asio::buffer(*rest),
                  boost::asio::transfer_exactly(rest_size),
                  boost::asio::bind_executor(
                      _self->strand_,
                      [success_byte, rest, next](
                          const boost::system::error_code& ec2, std::size_t) {
                        if (ec2) {
                          next(ec2, {});
                          return;
                        }

                        std::vector<std::byte> full;
                        full.reserve(1 + rest->size());
                        full.push_back((*success_byte)[0]);
                        full.insert(full.end(), rest->begin(), rest->end());

                        next({}, std::move(full));
                      }));
            }));
  }

  /**
   * Handle response STATUS
   *
   * @param operation
   */
  void handle_response_status(
      const std::shared_ptr<write_operation>& operation) {
    auto _self = shared_from_this();
    read_status_value(operation, [_self, operation](auto ec, auto response) {
      if (ec) {
        operation->handler(ec, {});
        _self->do_write();
        return;
      }
      operation->responses_.emplace_back(std::move(response));
      _self->handle_write(operation);
    });
  }

  /**
   * Strand
   */
  boost::asio::strand<boost::asio::any_io_executor> strand_;

  /**
   * Resolver
   */
  boost::asio::ip::tcp::resolver resolver_;

  /**
   * Socket
   */
  boost::asio::ip::tcp::socket socket_;

  /**
   * Host
   */
  std::string host_;

  /**
   * Port
   */
  uint16_t port_;

  /**
   * Queue
   */
  std::deque<write_operation> queue_;

  /**
   * Writing flag
   */
  bool writing_ = false;
};
}  // namespace throttr

#endif  // THROTTR_CONNECTION_HPP
