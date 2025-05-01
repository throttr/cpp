#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio.hpp>
#include <throttr/service.hpp>
#include <throttr/protocol.hpp>
#include <memory>
#include <thread>

using namespace boost::asio;
using namespace throttr;

using boost::asio::awaitable;
using boost::asio::use_awaitable;


class Benchmark {
public:
    Benchmark(io_context &io_context, service &service)
        : io_context_(io_context), service_(service) {
    }

    awaitable<void> consume_insert_only() {
        const std::string consumer_id = "consumer:insert-only";
        const std::string resource_id = "/api/insert-only";

        constexpr int total = 1'000'000;
        const auto ex = co_await this_coro::executor;

        std::atomic e = total;
        for (int i = 0; i < total; ++i) {
            co_spawn(io_context_, [this, consumer_id, resource_id, total, &e]() -> awaitable<void> {
                co_await service_.send<response_full>(
                        request_insert_builder(total, 1, ttl_types::seconds, 5, consumer_id, resource_id));
                --e;
                co_return;
            }, detached);
        }

        while (e.load(std::memory_order_acquire) > 0) {
            co_await steady_timer(io_context_, chrono::nanoseconds(100)).async_wait(use_awaitable);
        }

        co_return;
    }

    awaitable<void> consume_insert_and_update() const {
        const std::string consumer_id = "consumer:insert-update";
        const std::string resource_id = "/api/insert-update";

        constexpr int total = 100'000;

        const auto insert_consume = request_insert_builder(total, 1, ttl_types::seconds, 10, consumer_id, resource_id);
        const auto response = co_await service_.send<response_full>(insert_consume);
        if (!response.success) {
            co_return;
        }

        int remaining = total - 1;
        const auto ex = co_await this_coro::executor;

        while (remaining > 0) {
            co_await service_.send<response_simple>(
                request_update_builder(attribute_types::quota, change_types::decrease, 1, consumer_id, resource_id));
            --remaining;
        }

        co_return;
    }

    awaitable<void> run_benchmark() {
        std::cout << std::endl << "Benchmark #1 - Quota: 1.000.000, Usage: 1, TTL Type: Seconds - Using only Insert" <<
                std::endl;
        const auto start_insert_only = std::chrono::high_resolution_clock::now();
        co_await consume_insert_only();
        const auto end_insert_only = std::chrono::high_resolution_clock::now();
        std::cout << "Benchmark #1 completed. " << std::endl;
        const auto insert_only_elapsed_nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_insert_only - start_insert_only).count();
        const auto insert_only_elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_insert_only - start_insert_only).count();
        std::cout << "Time: " << insert_only_elapsed_nanoseconds << " nanoseconds." << std::endl;
        std::cout << "Time: " << insert_only_elapsed_milliseconds << " milliseconds." << std::endl << std::endl;

        std::cout << "Benchmark #2 - Quota: 10'000, Usage: 1, TTL Type: Seconds - Using Insert and Updates" << std::endl;
        const auto start_insert_and_update = std::chrono::high_resolution_clock::now();
        co_await consume_insert_and_update();
        const auto end_insert_and_update = std::chrono::high_resolution_clock::now();
        std::cout << "Benchmark #2 completed. " << std::endl;
        const auto insert_and_update_elapsed_nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(end_insert_and_update - start_insert_and_update).count();
        const auto insert_and_update_elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end_insert_and_update - start_insert_and_update).count();
        std::cout << "Time: " << insert_and_update_elapsed_nanoseconds << " nanoseconds." << std::endl;
        std::cout << "Time: " << insert_and_update_elapsed_milliseconds << " milliseconds." << std::endl << std::endl;

        co_return;
    }

private:
    io_context &io_context_;
    service &service_;
    std::atomic<int> pending_ = 10000;
};

int main() {
    io_context io_context;
    const service_config config = {"127.0.0.1", 9000, 10};
    service _service(io_context.get_executor(), config);

    std::atomic _connected = false;

    co_spawn(io_context, [&_service, &_connected]() -> awaitable<void> {
        std::puts("Running the service ...");
        co_await _service.connect();
        std::puts("Service is running ...");
        _connected = true;
        co_return;
    }, detached);

    co_spawn(io_context, [&_connected, &io_context, &_service]() -> awaitable<void> {
        while (!_connected) {
            std::puts("Waiting for service to be connected ...");
            co_await steady_timer(io_context, chrono::seconds(1)).async_wait(use_awaitable);
        }

        std::puts("Service is connected. Starting benchmark...");
        Benchmark benchmark(io_context, _service);
        co_await benchmark.run_benchmark();

        co_return;
    }, detached);


    std::vector<std::jthread> _threads_container;
    _threads_container.reserve(3);
    for (auto _i = 2; _i > 0; --_i)
        _threads_container.emplace_back([&io_context] { io_context.run(); });
    io_context.run();

    for (auto& _thead : _threads_container)
        _thead.join();

    return 0;
}
