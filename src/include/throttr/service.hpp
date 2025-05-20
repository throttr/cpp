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

#ifndef THROTTR_SERVICE_HPP
#define THROTTR_SERVICE_HPP

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <functional>
#include <memory>
#include <string>
#include <throttr/connection.hpp>
#include <throttr/response_get.hpp>
#include <throttr/response_query.hpp>
#include <throttr/response_status.hpp>
#include <vector>

namespace throttr {
/**
 * Service config
 */
struct service_config {
  std::string host_;
  uint16_t port_;
  size_t max_connections_ = 4;
};

/**
 * Service
 */
class service {
 public:
  /**
   * Constructor
   *
   * @param ex
   * @param cfg
   */
  service(boost::asio::any_io_executor ex, service_config cfg)
      : executor_(std::move(ex)), config_(std::move(cfg)) {}

  /**
   * Connect
   *
   * @param handler completion handler
   */
  void connect(std::function<void(boost::system::error_code)> handler) {
    auto _shared_handler =
        std::make_shared<std::function<void(boost::system::error_code)>>(
            std::move(handler));
    auto _pending =
        std::make_shared<std::atomic_size_t>(config_.max_connections_);
    auto _error_flag = std::make_shared<std::atomic_bool>(false);

    for (std::size_t _i = 0; _i < config_.max_connections_; ++_i) {
      auto _connection =
          std::make_shared<connection>(executor_, config_.host_, config_.port_);
      _connection->connect([this, _connection, _pending, _shared_handler,
                            _error_flag](boost::system::error_code ec) {
        if (!ec && !_error_flag->load()) {
          connections_.push_back(_connection);
        } else {
          // LCOV_EXCL_START
          _error_flag->store(true);
          // LCOV_EXCL_STOP
        }

        if (_pending->fetch_sub(1) == 1) {
          (*_shared_handler)(_error_flag->load()
                                 ? boost::asio::error::operation_aborted
                                 : boost::system::error_code{});
        }
      });
    }
  }

  /**
   * Is ready
   *
   * @return bool
   */
  [[nodiscard]] bool is_ready() const {
    return !connections_.empty() &&
           std::ranges::all_of(connections_,
                               [](const std::shared_ptr<connection>& c) {
                                 return c && c->is_open();
                               });
  }

  /**
   * Send raw
   *
   * @param buffer
   * @param handler
   */
  void send_raw(
      std::vector<std::byte> buffer,
      std::function<void(boost::system::error_code,
                         std::vector<std::vector<std::byte>>)> handler) {
    // LCOV_EXCL_START
    if (connections_.empty()) {
      // LCOV_EXCL_STOP
      handler(make_error_code(boost::system::errc::not_connected), {});
      return;
    }

    const auto _connection = get_connection();
    // LCOV_EXCL_START
    if (!_connection || !_connection->is_open()) {
      handler(make_error_code(boost::system::errc::connection_aborted), {});
      return;
    }
    // LCOV_EXCL_STOP

    _connection->send(std::move(buffer), std::move(handler));
  }

  template <typename... T, typename Handler>
  void send_many(Handler&& handler,
                 std::vector<std::vector<std::byte>> requests) {
    // LCOV_EXCL_START Note: Can't test
    if (connections_.empty()) {
      std::forward<Handler>(handler)(
          make_error_code(boost::system::errc::not_connected), T{}...);
      return;
    }

    const auto _conn = get_connection();
    if (!_conn || !_conn->is_open()) {
      std::forward<Handler>(handler)(
          make_error_code(boost::system::errc::connection_aborted), T{}...);
      return;
    }
    // LCOV_EXCL_STOP

    _conn->sendMany(
        requests, [_scoped_handler = std::forward<Handler>(handler)](
                      boost::system::error_code ec,
                      const std::vector<std::vector<std::byte>>& data) mutable {
          // LCOV_EXCL_START Note: Can't test
          if (ec || data.size() != sizeof...(T)) {
            std::move(_scoped_handler)(
                ec ? ec : make_error_code(boost::system::errc::protocol_error),
                T{}...);
            return;
          }
          // LCOV_EXCL_STOP

          call_with_parsed<T...>(data, std::move(_scoped_handler),
                                 std::index_sequence_for<T...>{});
        });
  }

  template <typename... T, std::size_t... I, typename Handler>
  static void call_with_parsed(const std::vector<std::vector<std::byte>>& data,
                               Handler&& handler,
                               std::index_sequence<I...>) {
    std::forward<Handler>(handler)({}, T::from_buffer(data[I])...);
  }

  /**
   * Send typed
   *
   * @tparam T
   * @param buffer
   * @param handler
   */
  template <typename T>
  void send(std::vector<std::byte> buffer,
            std::function<void(boost::system::error_code, T)> handler);

  /**
   * Get connection
   *
   * @return
   */
  std::shared_ptr<connection> get_connection() {
    const auto _idx = next_connection_index_.fetch_add(1) % connections_.size();
    return connections_[_idx];
  }

 private:
  /**
   * Executor
   */
  boost::asio::any_io_executor executor_;

  /**
   * Config
   */
  service_config config_;

  /**
   * Round-robin index
   */
  std::atomic<std::size_t> next_connection_index_{0};

  /**
   * Connections
   */
  std::vector<std::shared_ptr<connection>> connections_;
};

/**
 * Send implements T as response_status
 *
 * @return void
 */
template <>
inline void service::send<response_status>(
    std::vector<std::byte> buffer,
    std::function<void(boost::system::error_code, response_status)> handler) {
  send_raw(std::move(buffer), [_final_handler = std::move(handler)](
                                  auto ec, const auto& data) mutable {
    // LCOV_EXCL_START
    if (ec)
      return _final_handler(ec, {});
    // LCOV_EXCL_STOP
    _final_handler({}, response_status::from_buffer(data.at(0)));
  });
}

/**
 * Send implements T as response_status
 *
 * @return void
 */
template <>
inline void service::send<response_query>(
    std::vector<std::byte> buffer,
    std::function<void(boost::system::error_code, response_query)> handler) {
  send_raw(std::move(buffer), [_final_handler = std::move(handler)](
                                  auto ec, const auto& data) mutable {
    // LCOV_EXCL_START
    if (ec)
      return _final_handler(ec, {});
    // LCOV_EXCL_STOP
    _final_handler({}, response_query::from_buffer(data.at(0)));
  });
}

/**
 * Send implements T as response_get
 *
 * @return void
 */
template <>
inline void service::send<response_get>(
    std::vector<std::byte> buffer,
    std::function<void(boost::system::error_code, response_get)> handler) {
  send_raw(std::move(buffer), [_final_handler = std::move(handler)](
                                  auto ec, const auto& data) mutable {
    // LCOV_EXCL_START
    if (ec)
      return _final_handler(ec, {});
    // LCOV_EXCL_STOP

    _final_handler({}, response_get::from_buffer(data.at(0)));
  });
}

}  // namespace throttr

#endif  // THROTTR_SERVICE_HPP
