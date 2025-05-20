#include <throttr/service.hpp>
#include <throttr/response_status.hpp>
#include <throttr/protocol_wrapper.hpp>

#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace throttr;
using namespace boost::asio;

int main() {
    constexpr int thread_count = 4;
    constexpr int requests_per_thread = 250'000;
    constexpr int total_requests = thread_count * requests_per_thread;

    io_context io(thread_count);
    std::vector<std::unique_ptr<service>> services;

    std::atomic connected_count = 0;
    std::atomic failed = false;

    // Crear múltiples servicios (una conexión por cada hilo)
    for (int i = 0; i < thread_count; ++i) {
        auto svc = std::make_unique<service>(io.get_executor(), service_config{"throttr", 9000, 10});
        svc->connect([&failed, &connected_count](const boost::system::error_code &ec) {
            if (ec) {
                std::cerr << "Connection error: " << ec.message() << "\n";
                failed = true;
            }
            connected_count++;
        });
        services.emplace_back(std::move(svc));
    }

    // Esperar que todas las conexiones se completen
    while (connected_count.load() < thread_count)
        io.run_one();

    if (failed) {
        std::cerr << "Falló alguna conexión. Abortando.\n";
        return 1;
    }

    io.restart();

    const std::string key = "resource|consumer";
    const auto buffer = request_insert_builder(100, ttl_types::seconds, 10, key);

    // Distribuir envíos entre conexiones
    for (int t = 0; t < thread_count; ++t) {
        auto& svc = *services[t];
        for (int i = 0; i < requests_per_thread; ++i) {
            post(io, [buffer, &svc]() {
                svc.send<response_status>(buffer, [](auto, auto) {
                    // This is required
                });
            });
        }
    }

    std::puts("Running inserts...");
    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> pool; // NOSONAR
    for (int i = 0; i < thread_count; ++i)
        pool.emplace_back([&io] {
            // This should start to run the test
            io.run();
        });

    for (auto& t : pool) // NOSONAR
        t.join();

    std::puts("Finished.");

    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double seconds = ms / 1000.0;
    auto bytes = (buffer.size() + 1) * total_requests;

    std::cout << total_requests << " inserts in " << ms << " ms\n";
    std::cout << "Transferred: " << bytes / 1024.0 / 1024.0 << " MiB\n";
    std::cout << "Bandwidth: " << bytes / 1024.0 / 1024.0 / seconds << " MiB/s\n";
    std::cout << "Bandwidth: " << bytes / 1000.0 / 1024.0 / seconds << " MB/s\n";

    return 0;
}
