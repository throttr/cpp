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
#include <throttr/response_simple.hpp>
#include <throttr/response_full.hpp>
#include <throttr/protocol.hpp>
#include <throttr/connection.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

using namespace throttr;
using namespace boost::asio;

class ServiceTestFixture : public ::testing::Test {
public:
    io_context io;
    std::unique_ptr<service> svc;

    void SetUp() override {
        co_spawn(io, [this]() -> awaitable<void> {
            service_config cfg{ "throttr", 9000, 4 };
            svc = std::make_unique<service>(co_await this_coro::executor, cfg);
            co_await svc->connect();
            co_return;
        }, detached);
        io.run();
    }

    void TearDown() override {
        svc.reset();
    }
};

TEST_F(ServiceTestFixture, InsertAndQuerySuccessfully) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        const std::string consumer = "user:insert-and-query";
        const std::string resource = "/api/insert-and-query";

        const auto insert = request_insert_builder(
            5, 0,
            ttl_types::seconds, 5,
            consumer, resource
        );
        const auto insert_result = co_await svc->send<response_full>(insert);

        EXPECT_TRUE(insert_result.success);
        EXPECT_EQ(insert_result.quota_remaining, 5);
        EXPECT_EQ(insert_result.ttl_type, ttl_types::seconds);

        const auto query = request_query_builder(consumer, resource);
        const auto query_result = co_await svc->send<response_full>(query);

        EXPECT_TRUE(query_result.success);
        EXPECT_EQ(query_result.quota_remaining, 5);
        EXPECT_EQ(query_result.ttl_type, ttl_types::seconds);

        co_return;
    }, detached);
    io.run();
}

TEST_F(ServiceTestFixture, ConsumeViaInsert) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        const std::string consumer = "user:consume-insert";
        const std::string resource = "/api/consume-insert";

        co_await svc->send<response_full>(request_insert_builder(2, 0, ttl_types::seconds, 5, consumer, resource));

        for (int i = 0; i < 2; ++i) {
            auto insert_consume = request_insert_builder(0, 1, ttl_types::seconds, 5, consumer, resource);
            auto response = co_await svc->send<response_full>(insert_consume);
            EXPECT_TRUE(response.success);
            EXPECT_EQ(response.quota_remaining, 1 - i);
        }

        const auto query = request_query_builder(consumer, resource);
        const auto query_result = co_await svc->send<response_full>(query);
        EXPECT_TRUE(query_result.quota_remaining <= 0);
        co_return;
    }, detached);
    io.run();
}

TEST_F(ServiceTestFixture, UpdateDecreaseQuota) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        const std::string consumer = "user:update";
        const std::string resource = "/api/update";

        co_await svc->send<response_full>(request_insert_builder(2, 0, ttl_types::seconds, 5, consumer, resource));

        for (int i = 0; i < 3; ++i) {
            auto update = request_update_builder(attribute_types::quota, change_types::decrease, 1, consumer, resource);
            auto update_result = co_await svc->send<response_simple>(update);
            EXPECT_TRUE(update_result.success);
        }

        const auto query = request_query_builder(consumer, resource);
        const auto query_result = co_await svc->send<response_full>(query);
        EXPECT_EQ(query_result.quota_remaining, 0);
        co_return;
    }, detached);
    io.run();
}

TEST_F(ServiceTestFixture, PurgeThenQuery) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        const std::string consumer = "user:purge";
        const std::string resource = "/api/purge";

        co_await svc->send<response_full>(request_insert_builder(1, 0, ttl_types::seconds, 5, consumer, resource));

        const auto purge = request_purge_builder(consumer, resource);
        auto purge_response = co_await svc->send<response_simple>(purge);
        EXPECT_TRUE(purge_response.success);

        const auto query = request_query_builder(consumer, resource);
        const auto query_result = co_await svc->send<response_full>(query);
        EXPECT_FALSE(query_result.success);

        co_return;
    }, detached);
    io.run();
}

TEST_F(ServiceTestFixture, IsReadyReturnsFalseWhenNoConnections) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        EXPECT_FALSE(svc->is_ready());
        co_return;
    }, detached);
    io.run();
}

TEST_F(ServiceTestFixture, IsReadyReturnsTrueWhenAllConnectionsAreOpen) {
    io.restart();
    co_spawn(io, [this]() -> awaitable<void> {
        co_await svc->connect();

        EXPECT_TRUE(svc->is_ready());
        co_return;
    }, detached);
    io.run();
}
