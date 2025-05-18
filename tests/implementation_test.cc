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
#include <throttr/protocol.hpp>

class TcpConnectionTest : public ::testing::Test {
public:
    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket;

    TcpConnectionTest() : socket(io) {}

    void SetUp() override {
    }

    void TearDown() override {
        if (socket.is_open()) {
            boost::system::error_code ec;
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            socket.close(ec);
        }
    }
};

TEST_F(TcpConnectionTest, ConnectWaitAndDisconnect) {
    using boost::asio::ip::tcp;

    tcp::resolver resolver(io);
    const auto endpoints = resolver.resolve("127.0.0.1", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket, endpoints);
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    ASSERT_TRUE(socket.is_open());

    const auto buffer = throttr::request_insert_builder(
        1000,                                   // quota
        throttr::ttl_types::seconds,           // ttl type
        60,                                     // ttl = 60 sec
        "consumer:insert-only|api/insert-only"  // key
    );

    ASSERT_NO_THROW({
        boost::asio::write(socket, boost::asio::buffer(buffer));
    });

    std::vector<std::byte> response(64);
    boost::system::error_code ec;
    size_t n = socket.read_some(boost::asio::buffer(response), ec);

    ASSERT_FALSE(ec) << "Error on read: " << ec.message();

    std::cout << "Response (" << n << " bytes): ";
    for (size_t i = 0; i < n; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(std::to_integer<uint8_t>(response[i])) << " ";
    }
    std::cout << std::dec << std::endl;
}

TEST_F(TcpConnectionTest, ConcatenateTwoInsertsAndReadResponse) {
    using boost::asio::ip::tcp;

    tcp::resolver resolver(io);
    const auto endpoints = resolver.resolve("127.0.0.1", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket, endpoints);
    });

    const auto buffer1 = throttr::request_insert_builder(
        500, throttr::ttl_types::seconds, 30, "consumer:batch|api/test-1"
    );
    const auto buffer2 = throttr::request_insert_builder(
        750, throttr::ttl_types::seconds, 45, "consumer:batch|api/test-2"
    );

    std::vector<std::byte> concatenated;
    concatenated.reserve(buffer1.size() + buffer2.size());
    concatenated.insert(concatenated.end(), buffer1.begin(), buffer1.end());
    concatenated.insert(concatenated.end(), buffer2.begin(), buffer2.end());

    ASSERT_NO_THROW({
        boost::asio::write(socket, boost::asio::buffer(concatenated));
    });

    for (int i = 0; i < 2; ++i) {
        std::vector<std::byte> response(18);
        boost::system::error_code ec;
        size_t n = boost::asio::read(socket, boost::asio::buffer(response), ec);

        ASSERT_FALSE(ec) << "Error on read: " << i << ": " << ec.message();
        ASSERT_EQ(n, 18u) << "Response " << i << " incomplete";

        std::cout << "Response " << i << " (" << n << " bytes): ";
        for (size_t j = 0; j < n; ++j) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(std::to_integer<uint8_t>(response[j])) << " ";
        }
        std::cout << std::dec << std::endl;
    }
}

TEST_F(TcpConnectionTest, ConcatenateTwoInsertsAndReadFullResponseAtOnce) {
    using boost::asio::ip::tcp;

    tcp::resolver resolver(io);
    const auto endpoints = resolver.resolve("127.0.0.1", "9000");

    ASSERT_NO_THROW({
        boost::asio::connect(socket, endpoints);
    });

    const auto buffer1 = throttr::request_insert_builder(
        500, throttr::ttl_types::seconds, 30, "consumer:batch|api/test-1"
    );
    const auto buffer2 = throttr::request_insert_builder(
        750, throttr::ttl_types::seconds, 45, "consumer:batch|api/test-2"
    );

    std::vector<std::byte> concatenated;
    concatenated.reserve(buffer1.size() + buffer2.size());
    concatenated.insert(concatenated.end(), buffer1.begin(), buffer1.end());
    concatenated.insert(concatenated.end(), buffer2.begin(), buffer2.end());

    ASSERT_NO_THROW({
        boost::asio::write(socket, boost::asio::buffer(concatenated));
    });

    std::vector<std::byte> response(36);
    boost::system::error_code ec;
    const size_t n = boost::asio::read(socket, boost::asio::buffer(response), ec);

    ASSERT_FALSE(ec) << "Error on read: " << ec.message();
    ASSERT_EQ(n, 36u) << "Response wasn't 36 bytes";

    std::cout << "Response complete (" << n << " bytes): ";
    for (size_t i = 0; i < n; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(std::to_integer<uint8_t>(response[i])) << " ";
    }

    std::cout << std::dec << std::endl;
}