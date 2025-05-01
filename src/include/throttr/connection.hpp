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

#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace throttr {
    /**
     * Connection
     */
    class connection {
    public:
        /**
         * Constructor
         *
         * @param executor
         * @param host
         * @param port
         */
        connection(const boost::asio::any_io_executor &executor, std::string host, uint16_t port);

        /**
         * Connect
         * @return awaitable<void>
         */
        boost::asio::awaitable<void> connect();

        /**
         * Send
         *
         * @param buffer
         * @return awaitable<vector<byte>>
         */
        boost::asio::awaitable<std::vector<std::byte>> send(std::vector<std::byte> buffer);

        /**
         * Is open
         *
         * @return bool
         */
        [[nodiscard]] bool is_open() const;

    private:
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
    };

} // namespace throttr

#endif // THROTTR_CONNECTION_HPP
