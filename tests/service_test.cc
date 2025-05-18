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
#include <throttr/response_status.hpp>
#include <throttr/response_query.hpp>
#include <throttr/protocol.hpp>
#include <throttr/connection.hpp>

#include <boost/asio/io_context.hpp>


using namespace throttr;
using namespace boost::asio;

class ServiceTestFixture : public ::testing::Test {
public:
    io_context io;
    std::unique_ptr<service> svc;

    void SetUp() override {
        service_config cfg{ "throttr", 9000, 4 };
        svc = std::make_unique<service>(io.get_executor(), cfg);

        bool ready = false;
        svc->connect([&](boost::system::error_code ec) { // NOSONAR
            EXPECT_FALSE(ec);
            ready = true;
        });

        while (!ready) io.run_one(); // ejecuta solo lo necesario para conectar
        io.restart(); // ← ¡esta parte es clave!
    }

    void TearDown() override {
        svc.reset();
    }
};

TEST_F(ServiceTestFixture, InsertAndQuerySuccessfully) {

    ::testing::GTEST_FLAG(output) = "stream";
    std::cout << std::unitbuf; // Flush en cada línea

    const std::string key = "user:insert-and-query|/api/insert-and-query";

    bool finished = false;


    auto insert = request_insert_builder(5, ttl_types::seconds, 5, key);

    svc->send_raw(insert,
        [&](boost::system::error_code ec, std::vector<std::byte> raw_insert) { // NOSONAR
            std::cerr << "[Insert] ec: " << ec.message() << ", bytes: " << raw_insert.size() << "\n";
            if (ec) return;

            std::cerr << "[RAW_INSERT] ";
            for (auto b : raw_insert) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<int>(b) << " ";
            }
            std::cerr << std::dec << "\n"; // NOSONAR

            try {
                auto insert_result = response_query::from_buffer(raw_insert);

                EXPECT_TRUE(insert_result.success_);
                EXPECT_EQ(insert_result.quota_, 5);
                EXPECT_EQ(insert_result.ttl_type_, ttl_types::seconds);

                svc->send_raw(request_query_builder(key),
                    [&](boost::system::error_code ec2, std::vector<std::byte> raw_query) {
                        std::cerr << "[Query] ec: " << ec2.message() << ", bytes: " << raw_query.size() << "\n";
                        if (ec2) return;

                        try { // NOSONAR
                            auto query_result = response_query::from_buffer(raw_query);
                            EXPECT_TRUE(query_result.success_);
                            EXPECT_EQ(query_result.quota_, 5);
                            EXPECT_EQ(query_result.ttl_type_, ttl_types::seconds);
                            finished = true;
                        } catch (const std::exception& ex) { // NOSONAR
                            std::cerr << "[Query parse error] " << ex.what() << "\n";
                        }
                    });

            } catch (const std::exception& ex) { // NOSONAR
                std::cerr << "[Insert parse error] " << ex.what() << "\n";
            }
        });
    io.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, ConsumeViaInsert) {
    const std::string key = "user:consume-insert|/api/consume-insert";

    bool finished = false;

    auto do_query = [&]() {
        svc->send<response_query>(request_query_builder(key),
            [&](boost::system::error_code ec, response_query query_result) {
                ASSERT_FALSE(ec);
                EXPECT_TRUE(query_result.quota_ <= 0);
                finished = true;
            });
    };

    auto consume = [&](int i) {
        auto insert_consume = request_insert_builder(0, ttl_types::seconds, 5, key);
        svc->send<response_query>(insert_consume,
            [&, i](boost::system::error_code ec, response_query response) {
                ASSERT_FALSE(ec);
                EXPECT_TRUE(response.success_);
                EXPECT_EQ(response.quota_, 1 - i);
                if (i == 1) do_query();
            });
    };

    svc->send<response_query>(request_insert_builder(2, ttl_types::seconds, 5, key),
        [&](boost::system::error_code ec, response_query) {
            ASSERT_FALSE(ec);
            consume(0);
            consume(1);
        });

    io.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, UpdateDecreaseQuota) {
    const std::string key = "user:update|/api/update";
    bool finished = false;
    int updates = 0;

    auto do_query = [&]() {
        svc->send<response_query>(request_query_builder(key),
            [&](boost::system::error_code ec, response_query result) {
                ASSERT_FALSE(ec);
                EXPECT_EQ(result.quota_, 0);
                finished = true;
            });
    };

    auto do_update = [&]() {
        auto update = request_update_builder(attribute_types::quota, change_types::decrease, 1, key);
        svc->send<response_status>(update,
            [&](boost::system::error_code ec, response_status update_result) {
                ASSERT_FALSE(ec);
                EXPECT_TRUE(update_result.success_);
                if (++updates == 3) do_query();
            });
    };

    svc->send<response_query>(request_insert_builder(2, ttl_types::seconds, 5, key),
        [&](boost::system::error_code ec, response_query) {
            ASSERT_FALSE(ec);
            do_update();
            do_update();
            do_update();
        });

    io.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, PurgeThenQuery) {
    const std::string key = "user:purge|/api/purge";
    bool finished = false;

    svc->send<response_query>(request_insert_builder(1, ttl_types::seconds, 5, key),
        [&](boost::system::error_code ec, response_query) {
            ASSERT_FALSE(ec);
            svc->send<response_status>(request_purge_builder(key),
                [&](boost::system::error_code ec2, response_status purge_response) {
                    ASSERT_FALSE(ec2);
                    EXPECT_TRUE(purge_response.success_);

                    svc->send<response_query>(request_query_builder(key),
                        [&](boost::system::error_code ec3, response_query query_result) {
                            ASSERT_FALSE(ec3);
                            EXPECT_FALSE(query_result.success_);
                            finished = true;
                        });
                });
        });

    io.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, IsReadyReturnsTrueWhenAllConnectionsAreOpen) {
    EXPECT_TRUE(svc->is_ready());
}