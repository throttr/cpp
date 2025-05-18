# Throttr SDK for C++

<p align="center">
<a href="https://github.com/throttr/cpp/actions/workflows/build.yml"><img src="https://github.com/throttr/cpp/actions/workflows/build.yml/badge.svg" alt="Build"></a>
<a href="https://codecov.io/gh/throttr/cpp"><img src="https://codecov.io/gh/throttr/cpp/graph/badge.svg?token=5CVNFBAMTD" alt="Coverage"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=alert_status" alt="Quality Gate"></a>
</p>

<p align="center">
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=bugs" alt="Bugs"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=vulnerabilities" alt="Vulnerabilities"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=code_smells" alt="Code Smells"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=duplicated_lines_density" alt="Duplicated Lines"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=sqale_index" alt="Technical Debt"></a>
</p>

<p align="center">
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=reliability_rating" alt="Reliability"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=security_rating" alt="Security"></a>
<a href="https://sonarcloud.io/project/overview?id=throttr_cpp"><img src="https://sonarcloud.io/api/project_badges/measure?project=throttr_cpp&metric=sqale_rating" alt="Maintainability"></a>
</p>

C++ client for communicating with a Throttr server over TCP.

The SDK enables sending traffic control requests efficiently, without HTTP, respecting the server's native binary protocol.

## 🛠️ Installation

### Using CMake

Add the SDK as a submodule or fetch it directly. Then include it in your CMakeLists.txt:

```cmake
FetchContent_Declare(
    throttr-sdk
    GIT_REPOSITORY https://github.com/throttr/cpp.git
    GIT_TAG 2.0.0
)
FetchContent_MakeAvailable(throttr-sdk)

target_link_libraries(your_app PRIVATE throttr-sdk)
```

This SDK depends on Boost.Asio. Make sure Boost 1.87+ is available, with the following components:

- system
- thread
- coroutine
- charconv

## Basic Usage

```c++
#include <throttr/service.hpp>
#include <throttr/response_full.hpp>
#include <throttr/protocol_wrapper.hpp>

#include <boost/asio/io_context.hpp>
#include <iostream>

using namespace throttr;

int main() {
    boost::asio::io_context io;
    service_config cfg{ "throttr", 9000, 4 };

    auto svc = std::make_shared<service>(io.get_executor(), cfg);

    svc->connect([svc](const boost::system::error_code &connect_ec) {
        if (connect_ec) {
            std::cerr << "Connection failed: " << connect_ec.message() << "\n";
            return;
        }

        auto req = request_insert_builder(
            10, 0,
            ttl_types::seconds, 30,
            "user:abc", "/api/resource"
        );

        svc->send<response_full>(req, [svc](const boost::system::error_code &send_ec, response_full res) {
            if (send_ec) {
                std::cerr << "Send failed: " << send_ec.message() << "\n";
                return;
            }

            if (res.success) {
                std::cout << "Allowed ✅\n";
                std::cout << "Quota remaining: " << res.quota_remaining << "\n";
            } else {
                std::cout << "Denied ❌\n";
            }
        });
    });

    io.run();
    return 0;
}
```

See more examples in [tests](./tests/service_test.cc).

## Technical Notes

- The protocol assumes Little Endian architecture.
- The internal message queue ensures requests are processed sequentially.
- The package is defined to works with protocol 2.0.0 or greatest.

---

## License

Distributed under the [GNU Affero General Public License v3.0](./LICENSE).
