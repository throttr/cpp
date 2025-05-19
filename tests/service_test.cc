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
#include <throttr/response_get.hpp>
#include <throttr/protocol.hpp>
#include <throttr/connection.hpp>

#include <boost/asio/io_context.hpp>


using namespace throttr;
using namespace boost::asio;

class ServiceTestFixture : public ::testing::Test {
public:
    io_context io_;
    std::unique_ptr<service> svc_;

    void SetUp() override {
        service_config _cfg{ "throttr", 9000, 4 };
        svc_ = std::make_unique<service>(io_.get_executor(), _cfg);

        bool _ready = false;
        svc_->connect([&](boost::system::error_code ec) { // NOSONAR
            EXPECT_FALSE(ec);
            _ready = true;
        });

        while (!_ready) io_.run_one(); // ejecuta solo lo necesario para conectar
        io_.restart(); // ← ¡esta parte es clave!
    }

    void TearDown() override {
        svc_.reset();
    }
};

TEST_F(ServiceTestFixture, InsertAndQuerySuccessfully) {

    ::testing::GTEST_FLAG(output) = "stream";
    std::cout << std::unitbuf; // Flush en cada línea

    const std::string _key = "user:insert-and-query|/api/insert-and-query";

    bool _finished = false;

    const auto _insert = request_insert_builder(5, ttl_types::seconds, 5, _key);

    svc_->send_raw(_insert,
        [&](boost::system::error_code ec, const std::vector<std::byte> &raw_insert) { // NOSONAR
            std::cerr << "[Insert] ec: " << ec.message() << ", bytes: " << raw_insert.size() << "\n";
            if (ec) return;

            std::cerr << "[RAW_INSERT] ";
            for (const auto _b : raw_insert) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0') << std::to_integer<int>(_b) << " ";
            }
            std::cerr << std::dec << "\n"; // NOSONAR

            try {
                const auto insert_result = response_status::from_buffer(raw_insert);
                EXPECT_TRUE(insert_result.success_);

                svc_->send_raw(request_query_builder(_key),
                    [&](const boost::system::error_code &ec2, const std::vector<std::byte> &raw_query) {
                        std::cerr << "[Query] ec: " << ec2.message() << ", bytes: " << raw_query.size() << "\n";
                        if (ec2) return;

                        try { // NOSONAR
                            const auto query_result = response_query::from_buffer(raw_query);
                            EXPECT_TRUE(query_result.success_);
                            EXPECT_EQ(query_result.quota_, 5);
                            EXPECT_EQ(query_result.ttl_type_, ttl_types::seconds);
                            _finished = true;
                        } catch (const std::exception& ex) { // NOSONAR
                            std::cerr << "[Query parse error] " << ex.what() << "\n";
                        }
                    });

            } catch (const std::exception& ex) { // NOSONAR
                std::cerr << "[Insert parse error] " << ex.what() << "\n";
            }
        });
    io_.run();
    ASSERT_TRUE(_finished);
}

TEST_F(ServiceTestFixture, UpdateDecreaseQuota) {
    const std::string key = "user:update|/api/update";
    bool finished = false;
    int updates = 0;

    auto do_query = [&]() {
        svc_->send<response_query>(request_query_builder(key),
            [&](const boost::system::error_code &ec, const response_query result) {
                ASSERT_FALSE(ec);
                EXPECT_EQ(result.quota_, 0);
                finished = true;
            });
    };

    auto do_update = [&]() {
        auto update = request_update_builder(attribute_types::quota, change_types::decrease, 1, key);
        svc_->send<response_status>(update,
            [&](const boost::system::error_code &ec, const response_status update_result) {
                ASSERT_FALSE(ec);
                EXPECT_TRUE(update_result.success_);
                if (++updates == 3) do_query();
            });
    };

    svc_->send<response_status>(request_insert_builder(3, ttl_types::seconds, 5, key),
        [&](const boost::system::error_code &ec, response_status) {
            ASSERT_FALSE(ec);
            do_update();
            do_update();
            do_update();
        });

    io_.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, PurgeThenQuery) {
    const std::string key = "user:purge|/api/purge";
    bool finished = false;

    svc_->send<response_status>(request_insert_builder(1, ttl_types::seconds, 5, key),
        [&](const boost::system::error_code &ec, response_status) {
            ASSERT_FALSE(ec);
            svc_->send<response_status>(request_purge_builder(key),
                [&](const boost::system::error_code &ec2, const response_status purge_response) {
                    ASSERT_FALSE(ec2);
                    EXPECT_TRUE(purge_response.success_);

                    svc_->send<response_query>(request_query_builder(key),
                        [&](const boost::system::error_code &ec3, const response_query query_result) {
                            ASSERT_FALSE(ec3);
                            EXPECT_FALSE(query_result.success_);
                            finished = true;
                        });
                });
        });

    io_.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, SetThenGetFinallyPurge) {
    const std::string key = "user:set|/api/set";
    bool finished = false;
    const std::vector _buffer = {
        std::byte{'E'}, std::byte{'H'}, std::byte{'L'}, std::byte{'O'}
    };

    svc_->send<response_status>(request_set_builder(_buffer, ttl_types::seconds, 5, key),
        [&](const boost::system::error_code &ec, response_status) {
            ASSERT_FALSE(ec);

            svc_->send<response_get>(request_get_builder(key),
                                   [&](const boost::system::error_code &ec3, const response_get &get_result) {
                                       ASSERT_FALSE(ec3);
                                       EXPECT_TRUE(get_result.success_);

                                       EXPECT_EQ(get_result.value_[0], std::byte{0x45});
                                       EXPECT_EQ(get_result.value_[1], std::byte{0x48});
                                       EXPECT_EQ(get_result.value_[2], std::byte{0x4C});
                                       EXPECT_EQ(get_result.value_[3], std::byte{0x4F});
                                       EXPECT_EQ(get_result.value_.size(), 4);
                                       finished = true;

                                       svc_->send<response_status>(request_purge_builder(key),
                                           [&](const boost::system::error_code &ec2, const response_status purge_response) {
                                               ASSERT_FALSE(ec2);
                                               EXPECT_TRUE(purge_response.success_);
                                           });
                                   });
        });

    io_.run();
    ASSERT_TRUE(finished);
}

TEST_F(ServiceTestFixture, IsReadyReturnsTrueWhenAllConnectionsAreOpen) {
    EXPECT_TRUE(svc_->is_ready());
}