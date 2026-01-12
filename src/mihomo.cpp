#include "mihomo.h"

bool mihomo::change_proxy(const std::string & group_name, const std::string & proxy_name)
{
    httplib::Client http_cli(backend_address_, port_);
    http_cli.set_decompress(false);
    http_cli.set_read_timeout(3, 0);
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token_},
    };

    const std::string body = R"({"name": ")" + proxy_name +  "\"}";
    httplib::Result res;

    if (!token_.empty()) {
        res = http_cli.Put("/proxies/" + group_name, headers, body, "application/json");
    } else {
        res = http_cli.Put("/proxies/" + group_name, body, "application/json");
    }

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
    httplib::Client http_cli(backend_address_, port_);
    http_cli.set_decompress(false);
    http_cli.set_read_timeout(3, 0);
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token_},
    };

    std::string buffer;
    httplib::Result res;
    auto resp = [&](const char *data, const size_t len)
    {
        buffer.append(data, len);
        return true;
    };

    if (!token_.empty()) {
        res = http_cli.Get("/" + endpoint_name, headers, resp);
    } else {
        res = http_cli.Get("/" + endpoint_name, resp);
    }

    if (!res) {
        throw std::runtime_error(httplib::to_string(res.error()));
    }

    method(buffer);
}

bool mihomo::change_proxy_mode(const std::string& mode)
{
    httplib::Client http_cli(backend_address_, port_);
    http_cli.set_decompress(false);
    http_cli.set_read_timeout(3, 0);
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token_},
    };
    const std::string body = R"({"mode": ")" + mode +  "\"}";

    httplib::Result res;
    if (!token_.empty()) {
        res = http_cli.Patch("/configs", headers, body, "application/json");
    } else {
        res = http_cli.Patch("/configs", body, "application/json");
    }

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
    httplib::Client http_cli(backend_address_, port_);
    http_cli.set_decompress(false);
    http_cli.set_read_timeout(3, 0);
    const httplib::Headers headers = {
        {"Authorization", "Bearer " + token_},
    };

    httplib::Result res;
    if (!token_.empty()) {
        res = http_cli.Delete("/connections", headers);
    } else {
        res = http_cli.Delete("/connections");
    }

    if (!res) {
        std::cerr << "Request failed: " << httplib::to_string(res.error()) << "\n";
        return false;
    }

    if (res->status == 204) {
        return true;
    }

    return false;
}
