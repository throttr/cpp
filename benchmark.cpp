#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/strand.hpp>
#include <throttr/service.hpp>
#include <throttr/protocol.hpp>
#include <memory>

using namespace boost::asio;
using namespace throttr;

class Benchmark {
public:
    Benchmark(io_context& io_context, service& service)
        : io_context_(io_context), service_(service) {}

    // Función para consumir cuota
    boost::asio::awaitable<void> consume_quota() {
        const std::string consumer_id = "consumer:benchmark";
        const std::string resource_id = "/api/benchmark";

        while (quota_remaining_ > 0) {
            std::puts("Benchmark: Consumiendo cuota...");
            auto request = request_insert_builder(0, 1, ttl_types::seconds, 5, consumer_id, resource_id);
            auto response = co_await service_.send<throttr::response_full>(request);

            if (response.success) {
                std::puts("Benchmark: Consumo exitoso");
                quota_remaining_--;
            } else {
                std::puts("Benchmark: Error al consumir cuota");
            }
        }

        std::puts("Benchmark: Consumo de cuota completado");
        co_return;
    }

    // Ejecuta el benchmark con múltiples hilos
    void run_benchmark(int num_threads) {
        std::puts("Benchmark: Iniciando ejecución de benchmark");

        boost::asio::io_context::executor_type executor = io_context_.get_executor();

        // Medir el tiempo que toma
        auto start = std::chrono::high_resolution_clock::now();

        // Lanza las corrutinas para los hilos
        std::puts("Benchmark: Lanzando hilos...");
        std::vector<std::shared_ptr<std::function<boost::asio::awaitable<void>()>>> tasks;

        for (int i = 0; i < num_threads; ++i) {
            // Crear una lambda y almacenarla en un shared_ptr de function
            tasks.push_back(std::make_shared<std::function<boost::asio::awaitable<void>()>>([this]() -> awaitable<void> {
                std::puts("Benchmark: Hilo iniciado");
                co_await consume_quota();
                std::puts("Benchmark: Hilo finalizado");
                co_return;
            }));
        }

        // Ejecutamos todas las corrutinas de forma sincronizada
        for (auto& task : tasks) {
            co_spawn(executor, *task, detached);
        }

        // Ejecutamos el io_context para que las corrutinas se ejecuten
        std::puts("Benchmark: Ejecutando io_context...");
        io_context_.run();

        // Medimos el tiempo final
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        printf("Benchmark: Tiempo para consumir 1000 cuota con %d hilos: %.2f segundos.\n", num_threads, elapsed.count());
    }

private:
    io_context& io_context_;
    service& service_;
    std::atomic<int> quota_remaining_ = 1000;
};

int main() {
    io_context io_context;
    const service_config config = {"127.0.0.1", 9000, 4};
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
               benchmark.run_benchmark(10);  // Número de hilos que quieres usar

        co_return;
    }, detached);

    io_context.run();

    return 0;
}
