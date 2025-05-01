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

namespace throttr {
    service::service(boost::asio::any_io_executor ex, service_config cfg)
    : executor_(std::move(ex)), config_(std::move(cfg)) {}

    boost::asio::awaitable<void> service::connect() {
        co_await boost::asio::co_spawn(executor_, [this]() -> boost::asio::awaitable<void> {
            for (std::size_t i = 0; i < config_.max_connections_; ++i) {
                auto conn = std::make_shared<connection>(executor_, config_.host_, config_.port_);
                co_await conn->connect();
                connections_.push_back(conn);
            }
            co_return;
        }, boost::asio::use_awaitable);
    }

    bool service::is_ready() const {
        return !connections_.empty() &&
                   std::ranges::all_of(connections_,
                                       [](const std::shared_ptr<connection>& c) { return c && c->is_open(); });

    }

    std::shared_ptr<connection> service::get_connection() {
        const auto idx = next_connection_index_.fetch_add(1) % connections_.size();
        return connections_[idx];
    }

    boost::asio::awaitable<std::vector<std::byte>> service::send_raw(const std::vector<std::byte> &buffer) {
        if (connections_.empty()) {
            throw service_error("no available connections");
        }

        const auto conn = get_connection();
        if (!conn || !conn->is_open()) {
            throw service_error("get connection is not open");
        }

        co_return co_await conn->send(buffer);
    }
}
