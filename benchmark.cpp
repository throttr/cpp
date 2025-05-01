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

#include <throttr/service.hpp>
#include <throttr/response_simple.hpp>
#include <throttr/response_full.hpp>
#include <throttr/protocol.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>

using namespace throttr;
using namespace boost::asio;

int main() {
    io_context io { static_cast<int>(std::thread::hardware_concurrency()) };
    const service_config cfg = { "throttr", 9000, 10 };
    service svc(io.get_executor(), cfg);

    bool ready = false;
    svc.connect([&](boost::system::error_code ec) {
        if (ec) {
            std::cerr << "Connection error: " << ec.message() << "\n";
            return;
        }
        ready = true;
    });

    while (!ready) io.run_one();
    io.restart();

    constexpr int total = 100'000;
    const std::string resource = "resource";
    const std::string consumer = "consumer";
    std::atomic<int> remaining{total};
    auto start = std::chrono::steady_clock::now();
    auto buffer = request_insert_builder(100000, 1, ttl_types::seconds, 10, consumer, resource);
    for (int i = 0; i < total; ++i) {
        post(io, [&, buffer, &start, &remaining]() {
            svc.send<response_full>(buffer, [&](boost::system::error_code ec, response_full res) {
                if (remaining.fetch_sub(1) == 1) {
                    const auto end = std::chrono::steady_clock::now();
                    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                    std::cout << "100.000 insert has been done in " << ms << " ms\n";
                    io.stop();
                }
            });
        });
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < std::thread::hardware_concurrency(); ++i) {
        threads.emplace_back([&io]() { io.run(); });
    }
    for (auto& t : threads) t.join();

    return 0;
}
