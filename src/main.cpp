#include <string>
#include <iostream>
#include <vector>
#include <ranges>
#include <sys/stat.h>
#include <fstream>
#include "ccdb.h"
#include "general_info_pulling.h"

int main(int argc, char ** argv)
{
    std::string backend;
    int port = 0;
    std::string token;
    std::string latency_url = "https://www.google.com/generate_204/";

    try
    {
        if (argc >= 3)
        {
            backend = argv[1];
            port = static_cast<int>(std::strtol(argv[2], nullptr, 10));
        }

        if (argc >= 4) {
            token = argv[3];
        }

        if (argc == 5) {
            latency_url = argv[4];
        }

        if (argc < 3 || argc > 5)
        {
            std::cout << argv[0] << " [BACKEND] [PORT] <TOKEN> <LATENCY URL>" << std::endl;
            std::cout << " [...] is required, <...> is optional." << std::endl;
            return EXIT_FAILURE;
        }
    }
    catch (std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////////////////
    std::cout << "Connecting to http://" << backend << ":" << port << std::endl;
    std::cout << "C++ Clash Dashboard Version " << CCDB_VERSION << std::endl;
    ////////////////////////////////////////////////////////////////////////////////////////
    ccdb::ccdb ccdb(backend, port, token, latency_url);
    return EXIT_SUCCESS;
}
