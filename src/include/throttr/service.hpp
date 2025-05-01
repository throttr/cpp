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
#include <functional>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

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
         * @param handler completion handler
         */
        void connect(std::function<void(boost::system::error_code)> handler);

        /**
         * Is ready
         *
         * @return bool
         */
        [[nodiscard]] bool is_ready() const;

        /**
         * Send raw
         *
         * @param buffer
         * @param handler
         */
        void send_raw(std::vector<std::byte> buffer,
                      std::function<void(boost::system::error_code, std::vector<std::byte>)> handler);

        /**
         * Send typed
         *
         * @tparam T
         * @param buffer
         * @param handler
         */
        template<typename T>
        void send(std::vector<std::byte> buffer,
                  std::function<void(boost::system::error_code, T)> handler);

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
     * Send implements T as response_simple
     *
     * @return void
     */
    template<>
    inline void service::send<response_simple>(std::vector<std::byte> buffer,
                                               std::function<void(boost::system::error_code, response_simple)> handler) {
        send_raw(std::move(buffer), [final_handler = std::move(handler)](auto ec, const auto& data) mutable {
            if (ec) return final_handler(ec, {});
            final_handler({}, response_simple::from_buffer(data));
        });
    }

    /**
     * Send implements T as response_full
     *
     * @return void
     */
    template<>
    inline void service::send<response_full>(std::vector<std::byte> buffer,
                                             std::function<void(boost::system::error_code, response_full)> handler) {
        send_raw(std::move(buffer), [final_handler = std::move(handler)](auto ec, auto data) mutable {
            if (ec) return final_handler(ec, {});
            final_handler({}, response_full::from_buffer(data));
        });
    }

} // namespace throttr

#endif // THROTTR_SERVICE_HPP
