#include "mihomo.h"
#include "glogger.h"

bool mihomo::change_proxy(const std::string & group_name, const std::string & proxy_name)
{
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token},
    };

    const std::string body = R"({"name": ")" + proxy_name +  "\"}";
    auto res = http_cli.Put("/proxies/" + group_name, headers, body, "application/json");
    if (!res) {
        std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
        return false;
    }

    if (res->status == 204) {
        return true;
    }

    return false;
}

void mihomo::get_info_no_instance(const std::string & endpoint_name, const std::function < void(std::string) > & method)
{
    try
    {
        const httplib::Headers headers = {
            {"Authorization", "Bearer " + token},
        };

        std::string buffer;
        decltype(http_cli.Get("/")) res;
        {
            // std::lock_guard lock(http_cli_mutex);
            res = http_cli.Get("/" + endpoint_name, headers,
                [&](const char *data, const size_t len)
                {
                    buffer.append(data, len);
                    return true;
                }
            );
        }

        if (!res) {
            // logger.dlog("Request failed: ", httplib::to_string(res.error()), "\n");
            throw std::runtime_error(httplib::to_string(res.error()));
        }

        method(buffer);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error(e.what());
    }
}

bool mihomo::change_proxy_mode(const std::string& mode)
{
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token},
    };
    const std::string body = R"({"mode": ")" + mode +  "\"}";
    auto res = http_cli.Patch("/configs", headers, body, "application/json");
    if (!res) {
        std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
        return false;
    }

    if (res->status == 204) {
        return true;
    }

    return false;
}

bool mihomo::close_all_connections()
{
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token},
    };
    auto res = http_cli.Delete("/connections");
    if (!res) {
        std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
        return false;
    }

    if (res->status == 204) {
        return true;
    }

    return false;
}
