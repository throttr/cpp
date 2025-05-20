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
    io_context io;
    service_config cfg{"throttr", 9000, 10};
    service svc(io.get_executor(), cfg);

    std::atomic<bool> connected = false;
    std::atomic<bool> failed = false;

    svc.connect([&](boost::system::error_code ec) {
        if (ec) {
            std::cerr << "Connection error: " << ec.message() << "\n";
            failed = true;
            connected = true;
            return;
        }
        connected = true;
    });

    // Esperar conexión
    while (!connected)
        io.run_one();

    if (failed) {
        std::cerr << "No se pudo conectar. Abortando.\n";
        return 1;
    }

    io.restart();  // ¡Clave pa que no se quede detenido!

    constexpr int total = 100'000;
    const std::string key = "resource|consumer";
    const auto buffer = request_insert_builder(100, ttl_types::seconds, 10, key);

    for (int i = 0; i < total; ++i) {
        post(io, [&, buffer]() {
            svc.send<response_status>(buffer, [](auto, auto) {});
        });
    }

    const auto start = std::chrono::steady_clock::now();
    std::puts("Running inserts...");
    io.run();
    std::puts("Finished.");

    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    auto bytes = (buffer.size() + 1) * total;
    double seconds = ms / 1000.0;

    std::cout << total << " inserts in " << ms << " ms\n";
    std::cout << "Transferred: " << bytes << " bytes\n";
    std::cout << "Bandwidth: " << bytes / 1024.0 / seconds << " KiB/s\n";
    std::cout << "Bandwidth: " << bytes / 1000.0 / seconds << " kB/s\n";
    return 0;
}