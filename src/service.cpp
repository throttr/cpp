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

#include <memory>
#include <throttr/connection.hpp>
#include <throttr/exception.hpp>
#include <throttr/service.hpp>
#include <utility>

namespace throttr
{

service::service(boost::asio::any_io_executor ex, service_config cfg)
    : executor_(std::move(ex)), config_(std::move(cfg))
{
}

void service::connect(std::function<void(boost::system::error_code)> handler)
{
    auto _shared_handler =
        std::make_shared<std::function<void(boost::system::error_code)>>(std::move(handler));
    auto _pending    = std::make_shared<std::atomic_size_t>(config_.max_connections_);
    auto _error_flag = std::make_shared<std::atomic_bool>(false);

    for (std::size_t _i = 0; _i < config_.max_connections_; ++_i)
    {
        auto _connection = std::make_shared<connection>(executor_, config_.host_, config_.port_);
        _connection->connect(
            [this, _connection, _pending, _shared_handler, _error_flag](
                boost::system::error_code ec)
            {
                if (!ec && !_error_flag->load())
                {
                    connections_.push_back(_connection);
                }
                else
                {
                    // LCOV_EXCL_START
                    _error_flag->store(true);
                    // LCOV_EXCL_STOP
                }

                if (_pending->fetch_sub(1) == 1)
                {
                    (*_shared_handler)(_error_flag->load() ? boost::asio::error::operation_aborted
                                                           : boost::system::error_code{});
                }
            });
    }
}

bool service::is_ready() const
{
    return !connections_.empty() && std::ranges::all_of(connections_,
                                                        [](const std::shared_ptr<connection>& c)
                                                        { return c && c->is_open(); });
}

std::shared_ptr<connection> service::get_connection()
{
    const auto _idx = next_connection_index_.fetch_add(1) % connections_.size();
    return connections_[_idx];
}

void service::send_raw(
    std::vector<std::byte>                                                 buffer,
    std::function<void(boost::system::error_code, std::vector<std::byte>)> handler)
{
    if (connections_.empty())
    {
        handler(make_error_code(boost::system::errc::not_connected), {});
        return;
    }

    const auto _connection = get_connection();
    if (!_connection || !_connection->is_open())
    {
        // LCOV_EXCL_START
        handler(make_error_code(boost::system::errc::connection_aborted), {});
        return;
        // LCOV_EXCL_STOP
    }

    _connection->send(std::move(buffer), std::move(handler));
}

} // namespace throttr