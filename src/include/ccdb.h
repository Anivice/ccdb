#ifndef CCDB_H
#define CCDB_H

#include <vector>
#include <string>
#include <atomic>
#include <map>
#include <iomanip>
#include <termios.h>
#include "general_info_pulling.h"
#include "commandTemplateTree.h"

namespace ccdb
{
    class ccdb
    {
    private:
        termios old_tio { }, new_tio { };
        general_info_pulling backend_instance;
        const std::vector<std::string> titles = {
            "Host",         // 0
            "Process",      // 1
            "DL",           // 2
            "UP",           // 3
            "DL Speed",     // 4
            "UP Speed",     // 5
            "Rules",        // 6
            "Time",         // 7
            "Source IP",    // 8
            "Destination IP",   // 9
            "Type",         // 10
            "Chains",       // 11
        };

        std::atomic_int leading_spaces = 0, sort_by;
        std::atomic_bool reverse;
        std::map < uint64_t, std::string > index_to_proxy_name_list;
        std::map < std::string, int > latency_backups;
        std::map < std::string /* groups */, std::vector < std::string > /* endpoint */ > g_proxy_list;
        const std::string latency_url;

        void update_providers();

        static void nload(
            const std::atomic < uint64_t > * total_upload, const std::atomic < uint64_t > * total_download,
            const std::atomic < uint64_t > * upload_speed, const std::atomic < uint64_t > * download_speed,
            const std::atomic_bool * running, std::vector < std::string > & top_3_connections_using_most_speed,
            std::mutex * top_3_connections_using_most_speed_mtx);

        static void pager(const std::string & str, bool override_less_check = false, bool use_pager = true);

        static void print_table(
            std::vector<std::string> const & table_keys,
            std::vector < std::vector<std::string> > const & table_values,
            bool muff_non_ascii = true,
            bool seperator = true,
            const std::vector < bool > & table_hide = { },
            uint64_t leading_offset = 0,
            std::atomic_int * max_tailing_size_ptr = nullptr,
            bool using_less = false,
            const std::string & additional_info_before_table = "",
            int skip_lines = 0,
            std::atomic_int * max_skip_lines_ptr = nullptr,
            bool enforce_no_pager = false // disable line shrinking, used when NOPAGER=y or pager is not available
        );

        std::vector<std::string> get_groups();
        std::vector<std::string> get_endpoints(const std::string & group);
        std::vector<std::string> get_vgroups();
        std::vector<std::string> get_vendpoints(const std::string & group);

    public:
        void nload();
        void get_connections(const std::vector<std::string>& command_vector);
        void get_latency();
        void get_log();
        void get_proxy();
        void get_vecGroupProxy();
        void set_mode(const std::vector<std::string> & command_vector);
        void set_group(const std::vector<std::string> & command_vector);
        void set_vgroup(const std::vector<std::string> & command_vector);
        void set_chain_parser(const std::vector<std::string> & command_vector);
        void set_sort_by(const std::vector<std::string> & command_vector);
        void set_sort_reverse(const std::vector<std::string> & command_vector);
        void reset_terminal_mode();
        void set_conio_terminal_mode();
        ~ccdb();
        ccdb(const std::string & backend, int port, const std::string & token, std::string  latency_url_);
    };
}

#endif //CCDB_H