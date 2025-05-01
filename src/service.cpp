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

#include <throttr/service.hpp>

#include <throttr/connection.hpp>
#include <throttr/exception.hpp>
#include <utility>
#include <memory>

namespace throttr {

    service::service(boost::asio::any_io_executor ex, service_config cfg)
        : executor_(std::move(ex)), config_(std::move(cfg)) {}

    void service::connect(std::function<void(boost::system::error_code)> handler) {
        auto shared_handler = std::make_shared<std::function<void(boost::system::error_code)>>(std::move(handler));
        auto pending = std::make_shared<std::atomic_size_t>(config_.max_connections_);
        auto error_flag = std::make_shared<std::atomic_bool>(false);

        for (std::size_t i = 0; i < config_.max_connections_; ++i) {
            auto conn = std::make_shared<connection>(executor_, config_.host_, config_.port_);
            conn->connect([this, conn, pending, shared_handler, error_flag](boost::system::error_code ec) {
                if (!ec && !error_flag->load()) {
                    connections_.push_back(conn);
                } else {
                    error_flag->store(true);
                }

                if (pending->fetch_sub(1) == 1) {
                    (*shared_handler)(error_flag->load() ? boost::asio::error::operation_aborted : boost::system::error_code{});
                }
            });
        }
    }

    bool service::is_ready() const {
        return !connections_.empty() &&
               std::ranges::all_of(connections_, [](const std::shared_ptr<connection>& c) {
                   return c && c->is_open();
               });
    }

    std::shared_ptr<connection> service::get_connection() {
        const auto idx = next_connection_index_.fetch_add(1) % connections_.size();
        return connections_[idx];
    }

    void service::send_raw(std::vector<std::byte> buffer,
                           std::function<void(boost::system::error_code, std::vector<std::byte>)> handler) {
        if (connections_.empty()) {
            handler(make_error_code(boost::system::errc::not_connected), {});
            return;
        }


        const auto conn = get_connection();
        if (!conn || !conn->is_open()) {
            handler(make_error_code(boost::system::errc::connection_aborted), {});
            return;
        }

        conn->send(std::move(buffer), std::move(handler));
    }

}