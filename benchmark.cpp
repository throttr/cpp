#include <throttr/service.hpp>
#include <throttr/response_status.hpp>
#include <throttr/protocol_wrapper.hpp>

#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <iomanip>

using namespace throttr;
using namespace boost::asio;

int main() {
    constexpr int thread_count = 4;
    constexpr int requests_per_thread = 25'000;
    constexpr int total_requests = thread_count * requests_per_thread;

    io_context io(thread_count);
    std::vector<std::unique_ptr<service> > services;

    std::atomic connected_count = 0;
    std::atomic failed = false;

    for (int i = 0; i < thread_count; ++i) {
        auto svc = std::make_unique<service>(io.get_executor(), service_config{"throttr", 9000, 64});
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
    const auto buffer1 = request_insert_builder(32, ttl_types::seconds, 10, key);
    const auto buffer2 = request_query_builder(key);
    const auto buffer3 = request_query_builder(key);
    const auto buffer4 = request_query_builder(key);
    const auto buffer5 = request_query_builder(key);
    const auto buffer6 = request_query_builder(key);
    const auto buffer7 = request_query_builder(key);
    const auto buffer8 = request_query_builder(key);
    const auto buffer9 = request_query_builder(key);
    const auto buffer10 = request_query_builder(key);

    std::vector<std::byte> _concatenated;
    _concatenated.reserve(buffer1.size() + buffer2.size() + buffer3.size() + buffer4.size() + buffer5.size() + buffer6.size() + buffer7.size() + buffer8.size() + buffer9.size() + buffer10.size());
    _concatenated.insert(_concatenated.end(), buffer1.begin(), buffer1.end());
    _concatenated.insert(_concatenated.end(), buffer2.begin(), buffer2.end());
    _concatenated.insert(_concatenated.end(), buffer3.begin(), buffer3.end());
    _concatenated.insert(_concatenated.end(), buffer4.begin(), buffer4.end());
    _concatenated.insert(_concatenated.end(), buffer5.begin(), buffer5.end());
    _concatenated.insert(_concatenated.end(), buffer6.begin(), buffer6.end());
    _concatenated.insert(_concatenated.end(), buffer7.begin(), buffer7.end());
    _concatenated.insert(_concatenated.end(), buffer8.begin(), buffer8.end());
    _concatenated.insert(_concatenated.end(), buffer9.begin(), buffer9.end());
    _concatenated.insert(_concatenated.end(), buffer10.begin(), buffer10.end());


    // Distribuir envíos entre conexiones
    for (int t = 0; t < thread_count; ++t) {
        auto &svc = *services[t];
        for (int i = 0; i < requests_per_thread; ++i) {
            post(io, [&]() { // NOSONAR
                svc.send_raw(_concatenated,
                             [&]( // NOSONAR
                         const boost::system::error_code &ec,
                         const std::vector<std::vector<std::byte> > &) {
                                 if (ec) {
                                     std::cerr << "Error sending status: " << ec.message() << "\n";
                                 } else {
                                     // This is required ...
                                 }
                                 // This also...
                             });
            });
        }
    }

    std::puts("Running inserts and queries...");
    const auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> pool; // NOSONAR
    for (int i = 0; i < thread_count; ++i)
        pool.emplace_back([&io] {
            // This should start to run the test
            io.run();
        });

    for (auto &t: pool) // NOSONAR
        t.join();

    std::puts("Finished.");

    const auto end = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    double seconds = ms / 1000.0;
    auto bytes = (buffer1.size() + 1) * 10 * total_requests;

    std::cout << "1 insert and " << total_requests * 10 << " queries (pipeline) in " << ms << " ms\n";
    std::cout << "Transferred: " << bytes / 1024.0 / 1024.0 << " MiB\n";
    std::cout << "Bandwidth: " << bytes / 1024.0 / 1024.0 / seconds << " MiB/s\n";
    std::cout << "Bandwidth: " << bytes / 1000.0 / 1024.0 / seconds << " MB/s\n";

    return 0;
}
