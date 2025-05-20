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
#include <boost/asio/io_context.hpp>
#include <throttr/exception.hpp>
#include <throttr/service.hpp>

using namespace throttr;

class ServiceRawErrorTest : public ::testing::Test {
 public:
  boost::asio::io_context _io;
  std::unique_ptr<service> _svc;

  void SetUp() override {
    service_config _cfg{"throttr", 9000, 4};
    _svc = std::make_unique<service>(_io.get_executor(), _cfg);
  }

  void TearDown() override { _svc.reset(); }
};

TEST_F(ServiceRawErrorTest, ThrowsWhenNoConnectionsAvailable) {
  _io.restart();

  bool _error_triggered = false;

  const std::vector _dummy_buffer(1, std::byte{0x01});

  _svc->send_raw(_dummy_buffer,
                 [&](boost::system::error_code ec,
                     std::vector<std::vector<std::byte>>) {  // NOSONAR
                   if (ec == boost::system::errc::not_connected) {
                     _error_triggered = true;
                   }
                 });

  _io.run();

  ASSERT_TRUE(_error_triggered)
      << "Expected error due to no available connections";
}