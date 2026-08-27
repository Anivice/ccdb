#include "versions.h"
#include "openssl/opensslv.h"
#include "ncursesw/curses.h"
#include "readline/readline.h"
#include "json.hpp"
#include "print.h"
#include "httplib.h"
#include "absl/base/macros.h"
#include "maxminddb.h"

#define CCDB_VERSION_TO_TEXT(x) #x
#define CCDB_VERSION_TO_STRING(x) CCDB_VERSION_TO_TEXT(x)

#define VERSION_STRING \
    " OpenSSL: " OPENSSL_VERSION_TEXT "\n" \
    " ncurses: " NCURSES_VERSION "\n" \
    "readline: " CCDB_VERSION_TO_STRING(RL_VERSION_MAJOR) "." CCDB_VERSION_TO_STRING(RL_VERSION_MINOR) "\n" \
    " TSL Map: TSL Hopscotch-Hashing Map v2.4.0\n" \
    "    UTF8: 4.1.1\n" \
    "    JSON: " CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_MAJOR) "." CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_MINOR) "." CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_PATCH) "\n" \
    " HTTPLIB: " CPPHTTPLIB_VERSION "\n" \
    "    ABSL: " CCDB_VERSION_TO_STRING(ABSL_LTS_RELEASE_VERSION) "." CCDB_VERSION_TO_STRING(ABSL_LTS_RELEASE_PATCH_LEVEL) "\n" \
    "     RE2: 2025-11-05" "\n"

#ifdef __USE_IMG__

#include "CImg.h"
#include "zlib.h"
extern "C" {
#include "png.h"
}

#define IMG_LIB_VERSION \
    "CCDB has enabled image loading in terminal:\n" \
    "    CImg: " CCDB_VERSION_TO_STRING(cimg_version) "\n" \
    "    zlib: " ZLIB_VERSION "\n" \
    "  libpng: " PNG_LIBPNG_VER_STRING "\n" \
    "     TIV: Copyright © 2017-2023, Stefan Haustein, Aaron Liu. Modified by Anivice\n" \
    "     STB: stb_image - v2.30 - public domain image loader\n"
#else
#define IMG_LIB_VERSION ""
#endif

constexpr char version_suffix[] = VERSION_STRING IMG_LIB_VERSION;
version_string_ version_string;
version_string_::operator std::string()
{
    if (version.empty()) {
        version = ccdb::utils::sprint("C++ Clash Dashboard Version ", CCDB_VERSION,
        " (commit ", GIT_HASH, ", built on ", __DATE__, ")\n") + version_suffix + " MaxMind: " + MMDB_lib_version() + "\n";
        auto [fd_stdout, fd_stderr, status] = ccdb::utils::tar({ "/proc/self/exe", "--version" }, "");
        while (!fd_stdout.empty() && fd_stdout.back() == '\n') fd_stdout.pop_back();
        if (status == 0) version += "     tar:\n --- " + ccdb::utils::replace_all(fd_stdout, "\n", "\n --- ") + "\n";
    }

    return version;
}
