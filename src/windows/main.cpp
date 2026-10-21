#include "ccdb.h"
#include "args.h"
#include "Readline.h"
#include "LICENSE.h"
#include "windows_platform.h"

int main(int argc, char** argv)
{
    try {
        // Line-buffered diagnostics: an early failure must not lose its message.
        std::cout << std::unitbuf;
        ccdb::windows::console_session console;
        namespace utils = ccdb::utils;
        std::string backend, token, command;
        std::string latency_url = "https://www.google.com/generate_204";
        bool execute = false, quiet = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc) throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "-u" || arg == "--url") backend = value();
            else if (arg == "-t" || arg == "--token") token = value();
            else if (arg == "-l" || arg == "--latency_url") latency_url = value();
            else if (arg == "-x" || arg == "--execute") { command = value(); execute = true; }
            else if (arg == "-Q" || arg == "--quiet") quiet = true;
            else if (arg == "-h" || arg == "--help") {
                std::cout << "CCDB for Windows\n"
                    "  -u, --url URL        Mihomo HTTP controller (e.g. http://localhost:9090)\n"
                    "  -t, --token TOKEN    Controller secret\n"
                    "  -x, --execute CMD    Execute one command\n"
                    "  -l, --latency_url URL  URL tested by the backend\n"
                    "  -Q, --quiet          Suppress startup banner\n"
                    "  -v, --version        Version\n"
                    "  -V, --version-license  Version and license\n";
                return 0;
            } else if (arg == "-v" || arg == "--version" || arg == "-V" || arg == "--version-license") {
                std::cout << "CCDB " CCDB_VERSION " (native Windows / GNU Readline)\n";
                if (arg == "-V" || arg == "--version-license")
                    std::cout << utils::unpack_string(LICENSE, LICENSE_len);
                return 0;
            } else throw std::invalid_argument("Unknown option: " + arg);
        }
        if (backend.empty()) throw std::invalid_argument("Specify the Mihomo controller with --url.");
        if (!backend.starts_with("http://"))
            throw std::invalid_argument("This Windows build supports HTTP controller URLs only.");
        mihomo client(backend, token);
        const auto response = client.generic_request(
            static_cast<mihomo::DownloadMethod_path_prgrs>(&httplib::Client::Get), {}, "/");
        if (response->status != 200 || !nlohmann::json::parse(response->body).contains("hello"))
            throw std::runtime_error("Controller handshake failed (HTTP " + std::to_string(response->status) + ").");
        if (!quiet) std::cout << "CCDB " CCDB_VERSION " connected to " << backend << '\n';
        if (execute) {
            ccdb::ccdb app(backend, token, latency_url, utils::split_via_history(command));
        } else {
            ccdb::ccdb app(backend, token, latency_url, "", "");
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
