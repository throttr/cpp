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
#include <throttr/write_operation.hpp>
#include <throttr/protocol_wrapper.hpp>
#include <vector>

namespace throttr
{
/**
 * Connection
 */
class connection : public std::enable_shared_from_this<connection>
{
  public:
    /**
     * Constructor
     *
     * @param executor
     * @param host
     * @param port
     */
    connection(const boost::asio::any_io_executor& executor, std::string host, uint16_t port)
    : strand_(make_strand(executor)), resolver_(strand_), socket_(strand_), host_(std::move(host)),
      port_(port)
    {
    }

    /**
     * Connect
     *
     * @param handler
     */
    void connect(std::function<void(boost::system::error_code)> handler) {
        resolver_.async_resolve(host_,
                                std::to_string(port_),
                                [this, self = shared_from_this(), _scope_handler = std::move(handler)](
                                    const boost::system::error_code& ec, const auto& endpoints) mutable
                                {
                                    if (ec)
                                        return _scope_handler(ec);

                                    boost::asio::async_connect(
                                        socket_,
                                        endpoints,
                                        [self, _final_handler = std::move(_scope_handler)](
                                            const boost::system::error_code& connect_ec, auto) mutable
                                        { _final_handler(connect_ec); });
                                });
    };

    /**
     * Send
     *
     * @param buffer
     * @param handler
     */
    void send(std::vector<std::byte>                                                 buffer,
    std::function<void(boost::system::error_code, std::vector<std::byte>)> handler) {
        auto _self = shared_from_this();
        post(strand_,
             [_self, _scoped_buffer = std::move(buffer), _final_handler = std::move(handler)]() mutable
             {
                 _self->queue_.emplace_back(std::move(_scoped_buffer), std::move(_final_handler));
                 // LCOV_EXCL_START
                 if (!_self->writing_)
                 {
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
    [[nodiscard]] bool is_open() const {
        return socket_.is_open();
    }

  private:
    /**
     * Do write
     */
    void do_write() {
        if (queue_.empty())
        {
            writing_ = false;
            return;
        }

        auto _operation = std::make_shared<write_operation>(std::move(queue_.front()));
        queue_.pop_front();

        auto _self = shared_from_this();
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(_operation->buffer_),
            boost::asio::bind_executor(strand_,
                                       [_self, _operation](const boost::system::error_code& ec,
                                                           std::size_t /*bytes_transferred*/)
                                       {
                                           // LCOV_EXCL_START
                                           if (ec)
                                           {
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
    auto _self = shared_from_this();
    if (const auto _type = std::to_integer<uint8_t>(operation->buffer_[0]); _type == 0x02)
    {
        auto _head = std::make_shared<std::array<std::byte, 1>>();
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(*_head),
            boost::asio::transfer_exactly(1),
            boost::asio::bind_executor(
                strand_,
                [_self, operation, _head](
                    const boost::system::error_code& first_byte_ec, std::size_t)
                {
                    if (first_byte_ec)
                    {
                        operation->handler(first_byte_ec, {});
                        _self->do_write();
                        return;
                    }

                    if (const auto _status = std::to_integer<uint8_t>((*_head)[0]); _status == 0x00)
                    {
                        operation->handler({}, std::vector(_head->begin(), _head->end()));
                        _self->do_write();
                        return;
                    }

                    constexpr std::size_t _value_payload = sizeof(value_type) * 2 + 1;
                    auto _rest = std::make_shared<std::vector<std::byte>>(_value_payload);
                    boost::asio::async_read(
                        _self->socket_,
                        boost::asio::buffer(*_rest),
                        boost::asio::transfer_exactly(_value_payload),
                        boost::asio::bind_executor(
                            _self->strand_,
                            [_self, operation, _head, _rest](
                                const boost::system::error_code& partial_ec, std::size_t)
                            {
                                if (partial_ec)
                                {
                                    operation->handler(partial_ec, {});
                                }
                                else
                                {
                                    std::vector<std::byte> _full;
                                    _full.reserve(1 + _rest->size());
                                    _full.insert(_full.end(), _head->begin(), _head->end());
                                    _full.insert(_full.end(), _rest->begin(), _rest->end());
                                    operation->handler({}, std::move(_full));
                                }
                                _self->do_write();
                            }));
                }));
    }
    else if (_type == 0x06)
    {
        auto _head = std::make_shared<std::array<std::byte, 1>>();
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(*_head),
            boost::asio::transfer_exactly(1),
            boost::asio::bind_executor(
                strand_,
                [_self, operation, _head](
                    const boost::system::error_code& first_byte_ec, std::size_t)
                {
                    if (first_byte_ec)
                    {
                        operation->handler(first_byte_ec, {});
                        _self->do_write();
                        return;
                    }

                    if (const auto _status = std::to_integer<uint8_t>((*_head)[0]); _status == 0x00)
                    {
                        operation->handler({}, std::vector(_head->begin(), _head->end()));
                        _self->do_write();
                        return;
                    }

                    constexpr std::size_t N            = sizeof(uint16_t);
                    constexpr std::size_t _header_size = 1 + N + N;
                    auto _header = std::make_shared<std::vector<std::byte>>(_header_size);
                    boost::asio::async_read(
                        _self->socket_,
                        boost::asio::buffer(*_header),
                        boost::asio::transfer_exactly(_header_size),
                        boost::asio::bind_executor(
                            _self->strand_,
                            [_self, operation, _head, _header](
                                const boost::system::error_code& header_ec, std::size_t)
                            {
                                if (header_ec)
                                {
                                    operation->handler(header_ec, {});
                                    _self->do_write();
                                    return;
                                }

                                // Paso 3: decodificar M (size of value)
                                uint64_t _size = 0;
                                for (std::size_t i = 0; i < N; ++i)
                                {
                                    _size |= (std::to_integer<uint8_t>((*_header)[1 + N + i])
                                              << (i * 8));
                                }

                                // Paso 4: leer M bytes de valor
                                auto _value = std::make_shared<std::vector<std::byte>>(_size);
                                boost::asio::async_read(
                                    _self->socket_,
                                    boost::asio::buffer(*_value),
                                    boost::asio::transfer_exactly(_size),
                                    boost::asio::bind_executor(
                                        _self->strand_,
                                        [_self, operation, _head, _header, _value](
                                            const boost::system::error_code& value_ec, std::size_t)
                                        {
                                            if (value_ec)
                                            {
                                                operation->handler(value_ec, {});
                                            }
                                            else
                                            {
                                                std::vector<std::byte> _full;
                                                _full.reserve(1 + _header->size() + _value->size());
                                                _full.insert(
                                                    _full.end(), _head->begin(), _head->end());
                                                _full.insert(
                                                    _full.end(), _header->begin(), _header->end());
                                                _full.insert(
                                                    _full.end(), _value->begin(), _value->end());
                                                operation->handler({}, std::move(_full));
                                            }
                                            _self->do_write();
                                        }));
                            }));
                }));
    }
    else
    {
        auto _response_buffer = std::make_shared<std::vector<std::byte>>(1);
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(*_response_buffer),
            boost::asio::transfer_exactly(1),
            boost::asio::bind_executor(strand_,
                                       [_self, operation, _response_buffer](
                                           const boost::system::error_code& ec, std::size_t)
                                       {
                                           operation->handler(ec,
                                                              ec ? std::vector<std::byte>{}
                                                                 : std::move(*_response_buffer));
                                           _self->do_write();
                                       }));
    }
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

} // namespace throttr

#endif // THROTTR_CONNECTION_HPP