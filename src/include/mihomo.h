#ifndef MIHOMO_H
#define MIHOMO_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <any>
#include <stdexcept>
#include <thread>
#include <vector>
#include <ranges>

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

public:
    struct basic_info_overview_t
    {
        uint64_t download_speed;
        uint64_t upload_speed;
        uint64_t download_bytes;
        uint64_t upload_bytes;
    };

    template <typename InstanceType, typename... Args>
    void get_stream_info(
        InstanceType* instance,
        void (InstanceType::*method)(const std::atomic < basic_info_overview_t > &, Args...),
        Args&... args
    )
    {
        std::vector<std::pair<std::thread, std::unique_ptr<std::atomic < basic_info_overview_t >>>> pool;
        for (int i = 0; i < 32; i++)
        {
            std::vector<std::any> any_args = { args... };
            std::function<void(const std::atomic < basic_info_overview_t > &, const std::vector<std::any>&)> method_;
            pool.emplace_back(std::thread(), std::make_unique<std::atomic < basic_info_overview_t >>());
            auto & val = *pool.back().second;
            // Bind the method to the instance
            auto bound_function = [instance, method, this, &val](Args&... _args) -> void {
                (instance->*method)(val, _args...);
            };

            // Store a lambda that matches the signature of method_
            method_ = [bound_function, this](
                basic_info_overview_t,
                const std::vector<std::any>& _args) -> void
            {
                // Invoke the bound function with the provided arguments
                // The return value (std::any) is ignored since method_ expects void
                invoke_with_any<decltype(bound_function), Args...>(bound_function, _args);
            };

            basic_info_overview_t overview = {};
            overview.download_bytes = i;
            val.store(overview);
            // method_(shared_basic_info_overview_, any_args);
            std::thread T(method_, std::ref(val), any_args);
            pool.back().first = std::move(T);
        }

        for (auto & thread : pool | std::views::keys)
        {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
};

#endif //MIHOMO_H
