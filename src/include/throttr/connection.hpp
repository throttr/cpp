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
#include <boost/core/ignore_unused.hpp>
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
             const uint16_t port)
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
                if (!connect_ec) { // LCOV_EXCL_LINE
                  boost::asio::ip::tcp::no_delay option(true);
                  self->socket_.set_option(option);
                }
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
                               std::vector<std::vector<std::byte>>)> handler) {
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

  /**
   * Send many
   *
   * @param chunks
   * @param handler
   */
  void sendMany(
      const std::vector<std::vector<std::byte>>& chunks,
      std::function<void(boost::system::error_code,
                         std::vector<std::vector<std::byte>>)> handler) {
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
    // LCOV_EXCL_START Note: Partially tested
    if (operation->heads_.empty()) {
      // LCOV_EXCL_STOP
      operation->handler({}, std::move(operation->responses_));
      do_write();
      return;
    }

    const auto _type = std::to_integer<uint8_t>(operation->heads_.front());
    operation->heads_.erase(operation->heads_.begin());

    auto _self = shared_from_this();

    auto _continuation = [_self, operation](const boost::system::error_code& ec,
                                            std::vector<std::byte> response) {
      // LCOV_EXCL_START Note: Can't test
      if (ec) {
        operation->handler(ec, {});
        _self->do_write();
        return;
      }
      // LCOV_EXCL_STOP

      operation->responses_.emplace_back(std::move(response));
      _self->handle_write(operation);
    };

    switch (_type) {
      case 0x02: {
        const auto _head = std::make_shared<std::array<std::byte, 1>>();
        (*_head)[0] = std::byte{_type};
        read_query_value(operation, std::move(_continuation));
        break;
      }
      case 0x06: {
        const auto _head = std::make_shared<std::array<std::byte, 1>>();
        (*_head)[0] = std::byte{_type};
        read_get_header(operation, _head, std::move(_continuation));
        break;
      }
      default:
        read_status_value(operation, std::move(_continuation));
        break;
    }
  }

  void read_status_value(
      const std::shared_ptr<write_operation>& operation,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    auto _self = shared_from_this();
    auto _buf = std::make_shared<std::vector<std::byte>>(1);

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_buf), boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [_self, operation, _buf, next](
                         const boost::system::error_code &ec, std::size_t) {
              boost::ignore_unused(_self, operation);
              if (ec) {
                // LCOV_EXCL_START Note: Can't test
                next(ec, {});
                // LCOV_EXCL_STOP
              } else {
                next({}, std::move(*_buf));
              }
            }));
  }

  /**
   * Read GET header
   *
   * @param operation
   * @param head
   * @param next
   */
  void read_get_header(
      const std::shared_ptr<write_operation>& operation,
      const std::shared_ptr<std::array<std::byte, 1>>& head,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    const auto _self = shared_from_this();
    const auto _success = std::make_shared<std::array<std::byte, 1>>();

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_success),
        boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [next, _success, head, operation, _self](const boost::system::error_code& ec, std::size_t) {
              // LCOV_EXCL_START
              if (ec)
                return next(ec, {});
              // LCOV_EXCL_STOP

              // LCOV_EXCL_START
              if ((*_success)[0] == std::byte{0x00}) {
                std::vector<std::byte> _result;
                _result.push_back((*_success)[0]);
                return next({}, std::move(_result));
              }
              // LCOV_EXCL_STOP

              _self->read_get_header_continue(operation, head, _success, next);
            }));
  }

  /**
   * Continue reading GET header
   *
   * @param operation
   * @param head
   * @param success
   * @param next
   */
  void read_get_header_continue(
      const std::shared_ptr<write_operation>& operation,
      const std::shared_ptr<std::array<std::byte, 1>>& head,
      const std::shared_ptr<std::array<std::byte, 1>>& success,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    constexpr std::size_t header_size = 1 + sizeof(value_type) * 2;
    const auto _header = std::make_shared<std::vector<std::byte>>(header_size);

    const auto _self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(*_header),
        boost::asio::transfer_exactly(header_size),
        boost::asio::bind_executor(strand_, [_self, _header, next, operation, head, success](const boost::system::error_code &ec,
                                                std::size_t) {
                                                  // LCOV_EXCL_START
          if (ec)
            return next(ec, {});
          // LCOV_EXCL_STOP
          _self->read_get_header_value(operation, head, success, _header, next);
        }));
  }

  void read_get_header_value(
      const std::shared_ptr<write_operation>& operation,
      const std::shared_ptr<std::array<std::byte, 1>>& head,
      const std::shared_ptr<std::array<std::byte, 1>>& success,
      const std::shared_ptr<std::vector<std::byte>>& header,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    boost::ignore_unused(operation, head);

    value_type _value_size = 0;
    std::memcpy(&_value_size, header->data() + 1 + sizeof(value_type),
                sizeof(value_type));

    const auto _value = std::make_shared<std::vector<std::byte>>(_value_size);

    auto _self = shared_from_this();

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_value),
        boost::asio::transfer_exactly(_value_size),
        boost::asio::bind_executor(
            strand_, [_self, _value, header, success, next, operation](const boost::system::error_code& ec, std::size_t) {

              boost::ignore_unused(_self, operation);

              // LCOV_EXCL_START
              if (ec)
                return next(ec, {});
              // LCOV_EXCL_STOP

              std::vector<std::byte> _full;
              _full.reserve(1 + 1 + header->size() + _value->size());
              _full.push_back((*success)[0]);
              _full.insert(_full.end(), header->begin(), header->end());
              _full.insert(_full.end(), _value->begin(), _value->end());
              next({}, std::move(_full));
            }));
  }

  /**
   * Read query value
   *
   * @param operation
   * @param next
   */
  void read_query_value(
      const std::shared_ptr<write_operation>& operation,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    auto _self = shared_from_this();
    auto _success_byte = std::make_shared<std::array<std::byte, 1>>();

    boost::asio::async_read(
        socket_, boost::asio::buffer(*_success_byte),
        boost::asio::transfer_exactly(1),
        boost::asio::bind_executor(
            strand_, [this, _self, operation, _success_byte, next](
                         const boost::system::error_code& ec, std::size_t) {

              boost::ignore_unused(_self);

              // LCOV_EXCL_START
              if (ec) {
                next(ec, {});
                return;
              }
              // LCOV_EXCL_STOP

              handle_query_success_byte(operation, _success_byte, next);
            }));
  }

  /**
   * Handle query on success byte
   *
   * @param operation
   * @param success_byte
   * @param next
   */
  void handle_query_success_byte(
      const std::shared_ptr<write_operation>& operation,
      const std::shared_ptr<std::array<std::byte, 1>>& success_byte,
      const std::function<void(boost::system::error_code,
                               std::vector<std::byte>)>& next) {
    // LCOV_EXCL_START Note: Already tested
    if ((*success_byte)[0] == std::byte{0x00}) {
      // LCOV_EXCL_STOP
      next({}, {success_byte->begin(), success_byte->end()});
      return;
    }

    constexpr std::size_t _rest_size = sizeof(value_type) * 2 + 1;
    auto _rest = std::make_shared<std::vector<std::byte>>(_rest_size);

    auto _self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(*_rest),
        boost::asio::transfer_exactly(_rest_size),
        boost::asio::bind_executor(
            strand_, [this, _self, success_byte, _rest, next, operation](
                         const boost::system::error_code& ec2, std::size_t) {
              boost::ignore_unused(_self, operation);

              // LCOV_EXCL_START Note: Can't test
              if (ec2) {
                next(ec2, {});
                return;
              }
              // LCOV_EXCL_STOP

              handle_query_rest(success_byte, _rest, next);
            }));
  }

  /**
   * Handle query rest
   * @param success_byte
   * @param rest
   * @param next
   */
  template <typename Handler>
  static void handle_query_rest(
      const std::shared_ptr<std::array<std::byte, 1>>& success_byte,
      const std::shared_ptr<std::vector<std::byte>>& rest,
       Handler&& next) {
    std::vector<std::byte> _full;
    _full.reserve(1 + rest->size());
    _full.push_back((*success_byte)[0]);
    _full.insert(_full.end(), rest->begin(), rest->end());
    next({}, std::move(_full));
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
