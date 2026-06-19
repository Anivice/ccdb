#include "versions.h"
#include "openssl/opensslv.h"
#include "ncursesw/curses.h"
#include "readline/readline.h"
#include "json.hpp"
#include "print.h"
#include "GIT_HASH.h"

#define CCDB_VERSION_TO_TEXT(x) #x
#define CCDB_VERSION_TO_STRING(x) CCDB_VERSION_TO_TEXT(x)

#define VERSION_STRING \
    " OpenSSL: " OPENSSL_VERSION_TEXT "\n" \
    " ncurses: " NCURSES_VERSION "\n" \
    "readline: " CCDB_VERSION_TO_STRING(RL_VERSION_MAJOR) "." CCDB_VERSION_TO_STRING(RL_VERSION_MINOR) "\n" \
    " TSL Map: TSL Hopscotch-Hashing Map v2.4.0\n" \
    "    UTF8: 4.1.1\n" \
    "    JSON: " CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_MAJOR) "." CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_MINOR) "." CCDB_VERSION_TO_STRING(NLOHMANN_JSON_VERSION_PATCH) "\n" \
    "     TAR: tar (GNU tar) 1.35\n"

#ifdef __USE_IMG__
#define IMG_LIB_VERSION \
    "CCDB has enabled image loading in terminal:\n" \
    "    CImg: 3.7.5\n" \
    "    zlib: 1.3.2\n" \
    "  libpng: 1.6.58\n" \
    "     TIV: Copyright © 2017-2023, Stefan Haustein, Aaron Liu. Modified by Anivice\n" \
    "     STB: stb_image - v2.30 - public domain image loader\n"
#else
#define IMG_LIB_VERSION ""
#endif

const char * version_suffix = VERSION_STRING IMG_LIB_VERSION;
const char * g_version_string = nullptr;

class init_string_
{
    const std::string version =
        ccdb::utils::sprint("C++ Clash Dashboard Version ", CCDB_VERSION, " (commit ",
                ccdb_utils_unpack_string(GIT_HASH), ", built on ",
                __DATE__, ")\n") + version_suffix;
public:
    init_string_()
    {
        g_version_string = version.c_str();
    }
} init_string;
