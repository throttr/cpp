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
#include <throttr/response_status.hpp>
#include <throttr/response_query.hpp>
#include <throttr/protocol_wrapper.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/core/ignore_unused.hpp>
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
    svc.connect([&](boost::system::error_code ec) { // NOSONAR
        if (ec) {
            std::cerr << "Connection error: " << ec.message() << "\n";
            return;
        }
        ready = true;
    });

    while (!ready) io.run_one();
    io.restart();

    constexpr int total = 100;
    const std::string key = "resource|consumer";
    auto buffer = request_insert_builder(100, ttl_types::seconds, 10, key);
    for (int i = 0; i < total; ++i) {
        post(io, [&, buffer]() {
            svc.send<response_query>(buffer, [&](boost::system::error_code ec, response_query res) {
                boost::ignore_unused(ec, res);
            });
        });
    }

    const auto start = std::chrono::steady_clock::now();
    std::puts("Running ...");
    io.run();
    std::puts("Ran ...");
    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "100 insert has been done in " << ms << " ms\n";

    auto bytes_transferred = (buffer.size() + 1) * total; // payload + response
    double seconds = ms / 1000.0;

    std::cout << "Total transferred: " << bytes_transferred << " bytes\n";

    double kib_per_sec = bytes_transferred / 1024.0 / seconds;
    double mib_per_sec = kib_per_sec / 1024.0;

    double kb_per_sec  = bytes_transferred / 1000.0 / seconds;
    double mb_per_sec  = kb_per_sec / 1000.0;

    std::cout << "Bandwidth: " << kib_per_sec << " KiB/s\n";
    std::cout << "Bandwidth: " << mib_per_sec << " MiB/s\n";
    std::cout << "Bandwidth: " << kb_per_sec  << " kB/s\n";
    std::cout << "Bandwidth: " << mb_per_sec  << " MB/s\n";


    return 0;
}
