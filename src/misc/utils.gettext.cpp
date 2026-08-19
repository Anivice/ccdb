#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif //_GNU_SOURCE
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <regex>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <sys/syscall.h>
#include "json.hpp"
#include "lang.json.h"
#include "print.h"
#include "tsl/hopscotch_map.h"
#include "utils.h"
#include "dump.h"

#ifndef __NR_memfd_create
# if defined(__x86_64__)
#  define __NR_memfd_create 319
# elif defined(__i386__)
#  define __NR_memfd_create 356
# elif defined(__aarch64__)
#  define __NR_memfd_create 279
# else
#  error "Unknown architecture"
# endif
#endif

#define STRX(x) #x
#define STR(x) JSON_STRX(x)
#define CASSERT(x)  \
if (!(x)) {         \
    std::cout << __FILE__ ":" STR(__LINE__) ": Assertion " #x " Failed!\n"; \
    _exit(EXIT_FAILURE); \
}


using translator_t = tsl::hopscotch_map < std::string /* en text */, tsl::hopscotch_map < std::string /* lang */, std::string /* correct translation */ > >;
static std::unique_ptr < translator_t > text_translator;
static std::mutex text_translator_mtx;

std::string ccdb::utils::get_text(const std::string &text)
{
    static cache_w_freq_table_t < std::string, std::string > converted;
    if (const auto it = converted.get_cache(text); it) return *it;

    using json = nlohmann::json;

    std::lock_guard lock(text_translator_mtx);
    if (text_translator == nullptr) {
        text_translator = std::make_unique < translator_t >();
    }

    if (text_translator->empty())
    {
        std::string text_json_local;
        const auto data = decompress({lang_json, lang_json + lang_json_len});
        text_json_local.resize(data.size());
        std::memcpy(text_json_local.data(), data.data(), text_json_local.size());
        for (const auto text_data = json::parse(text_json_local);
            const auto & msg : text_data)
        {
            std::string text_en = msg["en"];
            std::ranges::transform(text_en, text_en.begin(), ::toupper);
            for (const auto & [type, lang_msg] : msg.items()) {
                const std::string type_str = type;
                const std::string lang_msg_str = lang_msg;
                if (!text_translator->contains(text_en)) text_translator->emplace(text_en, tsl::hopscotch_map < std::string , std::string >{});
                text_translator->at(text_en).emplace(type_str, lang_msg_str);
            }
        }
    }

    auto lang = getenv("LANG");
    auto cut = [&](const char c) {
        if (lang.find(c) != std::string::npos) {
            lang = lang.substr(0, lang.find_first_of(c));
        }
    };

    cut('.');

    std::string text_en = text;
    std::ranges::transform(text_en, text_en.begin(), ::toupper);
    if (text_translator->contains(text_en) && text_translator->at(text_en).contains(lang)) {
        const auto & result = text_translator->at(text_en).at(lang);
        converted.emplace_cache(text, result);
        return result;
    }
#ifdef RELEASE_CANDIDATE_PRE_RELEASE_BUILD
    static std::atomic_bool fs_check_completed = false;
    if (!fs_check_completed)
    {
        if (!std::filesystem::exists(getenv("HOME") + "/.config/ccdb/")) {
            try { std::filesystem::create_directories(getenv("HOME") + "/.config/ccdb/");
            } catch (const std::exception&) { }
        }

        if (!std::filesystem::exists(getenv("HOME") + "/.config/ccdb/MISSING-TRANSLATIONS.json")) {
            (void)open((getenv("HOME") + "/.config/ccdb/MISSING-TRANSLATIONS.json").c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        }

        fs_check_completed = true;
    }

    if (const int fd = open((getenv("HOME") + "/.config/ccdb/MISSING-TRANSLATIONS.json").c_str(),
        O_RDWR);
        fd > 0)
        [&]->void
        {
            class fd_
            {
            public:
                int ifd_ = -1;
                explicit fd_(const int fd) : ifd_(fd) { }
                ~fd_() { close(ifd_); }
            } fd_(fd);

            std::string json_raw;

            struct flock fl { };
            fl.l_type   = F_WRLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start  = 0;
            fl.l_len    = 0;
            fl.l_pid    = getpid();

            struct stat st = { };
            if (fstat(fd, &st) == -1) {
                return;
            }

            if (fcntl(fd, F_SETLKW, &fl) == -1) {
                return;
            }

            if (st.st_size > 0)
            {
                const auto data_ = static_cast<char*>(mmap(nullptr, st.st_size,
                    PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0));
                if (data_ == MAP_FAILED) {
                    return;
                }

                json_raw.insert(json_raw.end(), data_, data_ + st.st_size);
                munmap(data_, st.st_size);
            }

            try
            {
                if (json MISSING_TRANSLATIONS_json = !json_raw.empty() ? json::parse(json_raw) : json::array();
                    std::find(MISSING_TRANSLATIONS_json.begin(), MISSING_TRANSLATIONS_json.end(), text)
                    == MISSING_TRANSLATIONS_json.end())
                {
                    MISSING_TRANSLATIONS_json.emplace_back(text);
                    if (ftruncate(fd, 0) == -1) return;
                    const std::string dump = MISSING_TRANSLATIONS_json.dump();
                    (void)write(fd, dump.c_str(), dump.size());
                }
            }
            catch (std::exception&) {
                return;
            }
            fl.l_type = F_UNLCK;
            (void)fcntl(fd, F_SETLK, &fl);
        }();
#endif
    converted.emplace_cache(text, text);
    return text;
}
