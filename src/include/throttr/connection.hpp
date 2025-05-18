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


#include <throttr/write_operation.hpp>

#include <string>
#include <vector>
#include <functional>
#include <deque>
#include <mutex>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

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
        connection(const boost::asio::any_io_executor& executor, std::string host, uint16_t port);

        /**
         * Connect
         *
         * @param handler
         */
        void connect(std::function<void(boost::system::error_code)> handler);

        /**
         * Send
         *
         * @param buffer
         * @param handler
         */
        void send(std::vector<std::byte> buffer,
                  std::function<void(boost::system::error_code, std::vector<std::byte>)> handler);

        /**
         * Is open
         *
         * @return bool
         */
        [[nodiscard]] bool is_open() const;

    private:
        /**
         * Do write
         */
        void do_write();

        /**
         * Handle
         * @param operation
         */
        void handle_write(const std::shared_ptr<write_operation>& operation);

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

} // namespace throttr

#endif // THROTTR_CONNECTION_HPP