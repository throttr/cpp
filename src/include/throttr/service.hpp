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

#include <throttr/response_simple.hpp>
#include <throttr/response_full.hpp>

#include <vector>
#include <atomic>
#include <string>
#include <memory>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/awaitable.hpp>

namespace throttr {
    /**
     * Forward connection
     */
    class connection;

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
        service(boost::asio::any_io_executor ex, service_config cfg);

        /**
         * Connect
         *
         * @return awaitable<void>
         */
        boost::asio::awaitable<void> connect();

        /**
         * Is ready
         *
         * @return bool
         */
        [[nodiscard]] bool is_ready() const;


        /**
         * Send
         *
         * @param buffer
         * @return awaitable<response>
         */
        template<typename T>
        [[nodiscard]] boost::asio::awaitable<T> send(std::vector<std::byte> buffer);

        /**
         * Send
         *
         * @param buffer
         * @return awaitable<response>
         */
        [[nodiscard]] boost::asio::awaitable<std::vector<std::byte>> send_raw(std::vector<std::byte> buffer);

        /**
         * Get connection
         *
         * @return
         */
        std::shared_ptr<connection> get_connection();
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
     * Send
     *
     * @tparam T
     * @param buffer
     * @return
     */
    template<typename T>
    boost::asio::awaitable<T> service::send(const std::vector<std::byte> buffer) {
        auto raw = co_await send_raw(buffer);
        co_return T::from_buffer(raw);
    }

    /**
     * Send implements T as response_simple
     *
     * @return awaitable<response_simple>
     */
    template boost::asio::awaitable<response_simple> service::send<response_simple>(std::vector<std::byte>);

    /**
     * Send implements T as response_full
     *
     * @return awaitable<response_full>
     */
    template boost::asio::awaitable<response_full> service::send<response_full>(std::vector<std::byte>);
} // namespace throttr

#endif // THROTTR_SERVICE_HPP
