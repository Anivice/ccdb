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
#include "tsl/hopscotch_map.h"

namespace ccdb
{
    class ccdb
    {
    private:
        termios old_tio { }; // old TIO backup
        termios new_tio { }; // new TIO backup
        int old_flags = 0; // old flag backup
        bool terminal_mode_changed = false; // is term mode changed by ccdb?
        general_info_pulling backend_instance; // backend instance

        // get connections table: titles
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

        std::atomic_int sort_by = 4; // get connections table: sort by which column
        std::atomic_bool reverse = false; // get connections table: if sort is reversed?
        tsl::hopscotch_map < uint64_t, std::string > index_to_proxy_name_list; // vector translation list
        tsl::hopscotch_map < std::string, int > latency_backups; // results of latency test
        tsl::hopscotch_map < std::string /* groups */, std::vector < std::string > /* endpoint */ > g_proxy_list; // group-proxy list
        const std::string latency_url; // latency URL

        /// Pull groups and proxies from the backend
        void update_providers();

        /// print nload-like status update.
        /// This is supposed to be running in a separated thread
        /// @param total_upload atomic uint64_t of total uploaded bytes
        /// @param total_download atomic uint64_t of total downloaded bytes
        /// @param upload_speed atomic uint64_t of current upload speed
        /// @param download_speed atomic uint64_t of current download speed
        /// @param running atomic bool for thread running flag
        /// @param top_3_connections_using_most_speed // top 3 connections using most bandwidth
        /// @param top_3_connections_using_most_speed_mtx // mutex for top 3 connections using most bandwidth
        /// @returns NONE
        static void nload(
            const std::atomic < uint64_t > * total_upload, const std::atomic < uint64_t > * total_download,
            const std::atomic < uint64_t > * upload_speed, const std::atomic < uint64_t > * download_speed,
            const std::atomic_bool * running,
            std::vector < std::string > & top_3_connections_using_most_speed,
            std::mutex * top_3_connections_using_most_speed_mtx);

        /// show info using pager
        /// @param str content to be shown
        /// @param override_less_check Skip availability test
        /// @param use_pager pager availability flag. If override_less_check is true, then use_pager is dictated here
        /// otherwise, pager availability flag is set automatically.
        /// @returns NONE
        static void pager(const std::string & str, bool override_less_check = false, bool use_pager = true);

        /// print table
        /// @param table_keys Table titles
        /// @param table_values Each line for table entries
        /// @param muff_non_ascii Mask non ASCII characters. Already deprecated since non-ASCII support is included. Always set it to false
        /// @param seperator If seperator should be included in the table
        /// @param table_hide vector list for columns to hide when print
        /// @param leading_offset if the screen cannot fit the whole table, table will be shifted these characters to the right
        /// @param max_tailing_size_ptr Set by print_table, tells user I can only shift these many characters max
        /// @param using_pager Should I use pager? If so, all the above shifting parameters will be ignored
        /// @param additional_info_before_table Additional info to print before the table content
        /// @param skip_lines Skip this many lines and shift table downward when screen is too small to fit all the content
        /// @param max_skip_lines_ptr Set by print_table, tells user I can only shift downward this many lines max
        /// @param enforce_no_pager If using_pager is set to false, and max_tailing_size_ptr is valid, this will create an illusion that the table should shift
        /// according to the screen size. This flag is here to enforce that `using_pager = false` status and tells print_table to print the whole content
        /// without any sifting instead of partially trimmed content
        /// @returns NONE
        static void print_table(
            std::vector<std::string> const & table_keys,
            std::vector < std::vector<std::string> > const & table_values,
            bool muff_non_ascii = true,
            bool seperator = true,
            const std::vector < bool > & table_hide = { },
            uint64_t leading_offset = 0,
            std::atomic_int * max_tailing_size_ptr = nullptr,
            bool using_pager = false,
            const std::string & additional_info_before_table = "",
            int skip_lines = 0,
            std::atomic_int * max_skip_lines_ptr = nullptr,
            bool enforce_no_pager = false // disable line shrinking, used when NOPAGER=y or pager is not available
        );


        /// get proxy groups
        std::vector<std::string> get_groups();
        /// get proxy endpoint by group name
        std::vector<std::string> get_endpoints(const std::string & group);
        /// get vector proxy groups
        std::vector<std::string> get_vgroups();
        /// get vector proxy group endpoints
        std::vector<std::string> get_vendpoints(const std::string & group);

        // !! The following functions should be invoked by term mode guard and not the user !!
        // !! Create a term mode guard instead of invoking directly from here !!
        /// set no-echo and other term mode when continuous table update like nload is running
        void reset_terminal_mode();
        /// reset term mode back to the original
        void set_conio_terminal_mode();

    protected:
        // --- COMMANDS --- //

        void nload();
        void get_connections(const std::vector<std::string>& command_vector);
        void get_latency();
        void get_log();
        void get_proxy();
        void get_vecGroupProxy(bool show_vgroups = true);
        void set_mode(const std::vector<std::string> & command_vector);
        void set_group(const std::vector<std::string> & command_vector);
        void set_vgroup(const std::vector<std::string> & command_vector);
        void set_chain_parser(const std::vector<std::string> & command_vector);
        void set_sort_by(const std::vector<std::string> & command_vector);
        void set_sort_reverse(const std::vector<std::string> & command_vector);
        static void help();

        /// terminal mode guard. Create this instance to change and reset term mode automatically
        class mode_guard_t {
            ccdb * parent_;
        public:
            explicit mode_guard_t(ccdb * parent);
            ~mode_guard_t();
        };

        /// Input watcher that sets running flag when q is pressed
        /// @param name Thread name
        /// @param running Running flag
        void generic_input_watcher(const std::string & name, std::atomic_bool * running);

        /// Input watcher that sets running flag when q is pressed, and changes
        /// @param running_ptr Running flag
        /// @param leading_spaces_ptr Leading spaces, set by watcher from left/right/Home/End keys
        /// @param max_leading_spaces_ptr max leading space the watcher can set
        /// @param current_skip_lines_ptr Skip lines, set by up/down keys
        /// @param max_skip_lines_ptr max skip lines the watcher can set
        void get_conn_input_watcher(
            std::atomic_bool * running_ptr,
            std::atomic_int * leading_spaces_ptr,
            const std::atomic_int * max_leading_spaces_ptr,
            std::atomic_int * current_skip_lines_ptr,
            const std::atomic_int * max_skip_lines_ptr);

    public:
        ccdb(const std::string & backend, int port, const std::string & token, std::string  latency_url_);
        ~ccdb();

        friend class mode_guard_t;
    };
}

#endif //CCDB_H