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
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

using namespace throttr;
using namespace boost::asio;

class ServiceRawErrorTest : public ::testing::Test {
public:
    io_context io;
    std::unique_ptr<service> svc;

    void SetUp() override {
        service_config cfg{ "127.0.0.1", 9000, 4 };
        svc = std::make_unique<service>(io.get_executor(), cfg);
    }

    void TearDown() override {
        svc.reset();
    }
};

TEST_F(ServiceRawErrorTest, ThrowsWhenNoConnectionsAvailable) {
    io.restart();

    bool threw_expected = false;

    co_spawn(io, [this, &threw_expected]() -> awaitable<void> {
        const std::vector dummy_buffer(1, std::byte{0x01});
        try {
            co_await svc->send_raw(dummy_buffer);
        } catch (const service_error& e) {
            threw_expected = std::string(e.what()) == "no available connections";
        }
        co_return;
    }, detached);

    io.run();

    ASSERT_TRUE(threw_expected) << "Expected service_error with message 'no available connections'";
}