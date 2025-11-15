#include "mihomo.h"
#include "glogger.h"

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