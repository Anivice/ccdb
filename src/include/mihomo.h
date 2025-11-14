#ifndef MIHOMO_H
#define MIHOMO_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <any>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#include <ranges>
#include "httplib.h"
#include "json.hpp"

class mihomo
{
private:
    // Forward declaration of the helper
    template <typename... Args, std::size_t... I>
    std::tuple<Args...> any_cast_tuple_impl(const std::vector<std::any>& args, std::index_sequence<I...>)
    {
        return std::tuple<Args...>(std::any_cast<Args>(args[I])...);
    }

    // Primary template function
    template <typename... Args>
    std::tuple<Args...> any_cast_tuple(const std::vector<std::any>& args)
    {
        if (args.size() != sizeof...(Args)) {
            throw std::invalid_argument("Argument count mismatch");
        }

        return any_cast_tuple_impl<Args...>(args, std::make_index_sequence<sizeof...(Args)>{});
    }

    template <typename Func, typename... Args>
    std::any invoke_with_any(Func func, const std::vector<std::any>& args)
    {
        auto tuple_args = any_cast_tuple<Args...>(args);
        if constexpr (std::is_void_v<std::invoke_result_t<Func, Args...>>) {
            std::apply(func, tuple_args);
            return std::any{};
        } else {
            return std::apply(func, tuple_args);
        }
    }

    std::string token;
    httplib::Client http_cli;
    std::mutex http_cli_mutex;

public:
    explicit mihomo(const std::string& backend, int port, std::string token_) : token(std::move(token_)), http_cli(backend, port)
    {
        std::lock_guard lock(http_cli_mutex);
        http_cli.set_decompress(false);
    }

    ~mihomo() = default;

    template <typename InstanceType, typename... Args>
    void get_info(const std::string & endpoint_name, InstanceType* instance,
        void (InstanceType::*method)(std::pair < std::mutex, std::string > &, Args...),
        Args&... args)
    {
        try
        {
            httplib::Headers headers = {
                {"Authorization", "Bearer " + token},
            };

            std::vector<std::any> any_args = { args... };
            std::function<void(std::pair < std::mutex, std::string > &, const std::vector<std::any>&)> method_;
            auto val = std::make_unique < std::pair < std::mutex, std::string > >();
            // Bind the method to the instance
            auto bound_function = [instance, method, this, &val](Args&... _args) -> void {
                (instance->*method)(*val, _args...);
            };

            // Store a lambda that matches the signature of method_
            method_ = [bound_function, this](
                std::pair < std::mutex, std::string > &,
                const std::vector<std::any>& _args) -> void
            {
                // Invoke the bound function with the provided arguments
                // The return value (std::any) is ignored since method_ expects void
                invoke_with_any<decltype(bound_function), Args...>(bound_function, _args);
            };

            std::string buffer;
            decltype(http_cli.Get("/")) res;
            {
                std::lock_guard lock(http_cli_mutex);
                res = http_cli.Get("/" + endpoint_name, headers,
                    [&](const char *data, const size_t len)
                    {
                        buffer.append(data, len);
                        return true;
                    }
                );
            }

            if (!res) {
                std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
                throw std::runtime_error(httplib::to_string(res.error()));
            }

            {
                std::lock_guard lock(val->first);
                val->second = buffer;
            }
            method_(std::ref(*val), any_args);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(e.what());
        }
    }

    template <typename InstanceType, typename... Args>
    void get_stream_info(
        const std::string & endpoint_name,
        const std::atomic_bool * keep_running,
        InstanceType* instance,
        void (InstanceType::*method)(std::pair < std::mutex, std::string > &, Args...),
        Args&... args
    )
    {
        try
        {
            std::atomic_bool stance(true);
            auto worker = [&]()->void
            {
                std::string buffer;
                std::vector<std::pair<std::thread, std::unique_ptr<std::pair < std::mutex, std::string >>>> thread_pool;
                httplib::Headers headers = {
                    {"Authorization", "Bearer " + token},
                };

                std::lock_guard lock(http_cli_mutex);
                http_cli.Get("/" + endpoint_name, headers,
                    [&](const char *data, const size_t len)
                {
                    buffer.append(data, len);
                    if (const auto pos = buffer.find('\n'); pos != std::string::npos)
                    {
                        const std::string first_line = buffer.substr(0, pos);
                        buffer = buffer.substr(pos + 1);
                        std::vector<std::any> any_args = { args... };
                        std::function<void(std::pair < std::mutex, std::string > &, const std::vector<std::any>&)> method_;
                        thread_pool.emplace_back(std::thread(), std::make_unique < std::pair < std::mutex, std::string > >());
                        auto & val = *thread_pool.back().second;
                        // Bind the method to the instance
                        auto bound_function = [instance, method, this, &val](Args&... _args) -> void {
                            (instance->*method)(val, _args...);
                        };

                        // Store a lambda that matches the signature of method_
                        method_ = [bound_function, this](
                            std::pair < std::mutex, std::string > &,
                            const std::vector<std::any>& _args) -> void
                        {
                            // Invoke the bound function with the provided arguments
                            // The return value (std::any) is ignored since method_ expects void
                            invoke_with_any<decltype(bound_function), Args...>(bound_function, _args);
                        };

                        {
                            std::lock_guard lock_val(val.first);
                            val.second = first_line;
                        }
                        std::thread T(method_, std::ref(val), any_args);
                        thread_pool.back().first = std::move(T);

                        if (thread_pool.size() > 32) // oversized pool cleanup
                        {
                            for (auto & thread : thread_pool | std::views::keys)
                            {
                                if (thread.joinable()) {
                                    thread.join();
                                }
                            }

                            thread_pool.clear();
                        }

                        return stance.load(); // keep_running->load();
                    }

                    return true;
                });

                for (auto & thread : thread_pool | std::views::keys)
                {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
            };

            while (*keep_running)
            {
                stance = true;
                std::thread T(worker);
                std::this_thread::sleep_for(std::chrono::seconds(1l));
                stance = false;
                if (T.joinable()) {
                    T.join();
                }
            }
        } catch (std::exception & e) {
            throw std::runtime_error(e.what());
        } catch (...) {
            throw std::runtime_error("Unknown error");
        }
    }
};

#endif //MIHOMO_H
