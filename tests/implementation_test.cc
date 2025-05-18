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

#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <thread>
#include <chrono>
#include <throttr/protocol_wrapper.hpp>

class TcpConnectionTest : public ::testing::Test {
public:
    boost::asio::io_context io_;
    boost::asio::ip::tcp::socket socket_;

    TcpConnectionTest() : socket_(io_) {}

    void SetUp() override {
    }

    void TearDown() override {
        if (socket_.is_open()) {
            boost::system::error_code ec;
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket_.close(ec);
        }
    }
};

TEST_F(TcpConnectionTest, ConnectWaitAndDisconnect) {
    using boost::asio::ip::tcp;

    tcp::resolver _resolver(io_);
    const auto _endpoints = _resolver.resolve("throttr", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket_, _endpoints);
    });

    ASSERT_TRUE(socket_.is_open());

    const auto _buffer = throttr::request_insert_builder(
        1000,                                   // quota
        throttr::ttl_types::seconds,           // ttl type
        60,                                     // ttl = 60 sec
        "consumer:insert-only|api/insert-only"  // key
    );

    ASSERT_NO_THROW({
        boost::asio::write(socket_, boost::asio::buffer(_buffer));
    });

    std::vector<std::byte> _response(1);
    boost::system::error_code _ec;
    size_t _n = socket_.read_some(boost::asio::buffer(_response), _ec);

    ASSERT_FALSE(_ec) << "Error on read: " << _ec.message();

    std::cout << "Response (" << _n << " bytes): ";
    for (size_t _i = 0; _i < _n; ++_i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(std::to_integer<uint8_t>(_response[_i])) << " ";
    }
    std::cout << std::dec << std::endl;
}

TEST_F(TcpConnectionTest, ConcatenateTwoInsertsAndReadResponse) {
    using boost::asio::ip::tcp;

    tcp::resolver _resolver(io_);
    const auto _endpoints = _resolver.resolve("throttr", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket_, _endpoints);
    });

    const auto _buffer1 = throttr::request_insert_builder(
        500, throttr::ttl_types::seconds, 30, "consumer:batch|api/test-1"
    );
    const auto _buffer2 = throttr::request_insert_builder(
        750, throttr::ttl_types::seconds, 45, "consumer:batch|api/test-2"
    );

    std::vector<std::byte> _concatenated;
    _concatenated.reserve(_buffer1.size() + _buffer2.size());
    _concatenated.insert(_concatenated.end(), _buffer1.begin(), _buffer1.end());
    _concatenated.insert(_concatenated.end(), _buffer2.begin(), _buffer2.end());

    ASSERT_NO_THROW({
        boost::asio::write(socket_, boost::asio::buffer(_concatenated));
    });

    for (int _i = 0; _i < 2; ++_i) {
        std::vector<std::byte> _response(1);
        boost::system::error_code _ec;
        size_t _n = boost::asio::read(socket_, boost::asio::buffer(_response), _ec);

        ASSERT_FALSE(_ec) << "Error on read: " << _i << ": " << _ec.message();
        ASSERT_EQ(_n, 1u) << "Response " << _i << " incomplete";

        std::cout << "Response " << _i << " (" << _n << " bytes): ";
        for (size_t _j = 0; _j < _n; ++_j) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(std::to_integer<uint8_t>(_response[_j])) << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

TEST_F(TcpConnectionTest, ConcatenateTwoInsertsAndQueriesAtOnce) {
    using boost::asio::ip::tcp;

    tcp::resolver _resolver(io_);
    const auto _endpoints = _resolver.resolve("throttr", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket_, _endpoints);
    });

    const auto _buffer1 = throttr::request_insert_builder(
        5, throttr::ttl_types::seconds, 7, "consumer:batch|api/test-1"
    );
    const auto _buffer2 = throttr::request_insert_builder(
        1, throttr::ttl_types::seconds, 9, "consumer:batch|api/test-2"
    );
    const auto _buffer3 = throttr::request_query_builder("consumer:batch|api/test-1");
    const auto _buffer4 = throttr::request_query_builder("consumer:batch|api/test-2");

    std::vector<std::byte> _concatenated;
    _concatenated.reserve(_buffer1.size() + _buffer2.size() + _buffer3.size() + _buffer4.size());
    _concatenated.insert(_concatenated.end(), _buffer1.begin(), _buffer1.end());
    _concatenated.insert(_concatenated.end(), _buffer2.begin(), _buffer2.end());
    _concatenated.insert(_concatenated.end(), _buffer3.begin(), _buffer3.end());
    _concatenated.insert(_concatenated.end(), _buffer4.begin(), _buffer4.end());

    ASSERT_NO_THROW({
        boost::asio::write(socket_, boost::asio::buffer(_concatenated));
    });

    int _expected_length = 4 + 2 + sizeof(throttr::value_type) * 4;
    std::vector<std::byte> _response(_expected_length);
    boost::system::error_code _ec;
    const size_t _n = boost::asio::read(socket_, boost::asio::buffer(_response), _ec);

    ASSERT_FALSE(_ec) << "Error on read: " << _ec.message();
    ASSERT_EQ(_n, _expected_length) << "Response wasn't the expected bytes";

    std::cout << "Response complete (" << _n << " bytes): ";
    for (size_t _i = 0; _i < _n; ++_i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(std::to_integer<uint8_t>(_response[_i])) << " ";
    }

    std::cout << std::dec << std::endl;
}