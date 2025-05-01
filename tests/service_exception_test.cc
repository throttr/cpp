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

#include <gtest/gtest.h>
#include <throttr/service.hpp>
#include <throttr/exception.hpp>
#include <boost/asio/io_context.hpp>

using namespace throttr;

class ServiceRawErrorTest : public ::testing::Test {
public:
    boost::asio::io_context io;
    std::unique_ptr<service> svc;

    void SetUp() override {
        service_config cfg{ "throttr", 9000, 4 };
        svc = std::make_unique<service>(io.get_executor(), cfg);
    }

    void TearDown() override {
        svc.reset();
    }
};

TEST_F(ServiceRawErrorTest, ThrowsWhenNoConnectionsAvailable) {
    io.restart();

    bool error_triggered = false;

    const std::vector dummy_buffer(1, std::byte{0x01});

    svc->send_raw(dummy_buffer, [&](boost::system::error_code ec, std::vector<std::byte>) { // NOSONAR
        if (ec == boost::system::errc::not_connected) {
            error_triggered = true;
        }
    });

    io.run();

    ASSERT_TRUE(error_triggered) << "Expected error due to no available connections";
}