#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
#include "libpsl.h"

namespace url_sort {

static std::optional<std::string> normalize_host_with_libpsl(std::string_view host) 
{
    char* lower = nullptr;
    const psl_error_t rc =
        psl_str_to_utf8lower(std::string(host).c_str(), nullptr, nullptr, &lower);
    if (rc != PSL_SUCCESS || lower == nullptr) {
        return std::nullopt;
    }

    std::string out(lower);
    psl_free_string(lower);
    return out;
}

struct ParsedHostPort {
    std::string host;        // normalized host
    std::uint16_t port = 0;  // validated numeric port
};

struct SortKey {
    bool matched = false;

    std::string registrable_reversed; // e.g. "com.example", "uk.co.example"
    std::string registrable_domain;   // e.g. "example.com", "example.co.uk"
    std::string subdomain_prefix;     // e.g. "a.b" for a.b.example.co.uk
    std::uint16_t port = 0;
    std::string normalized_host;      // lowercase host, no trailing dot
    std::string original;             // original input for strict tie-break
};

constexpr bool kAllowUnderscoreInLabels = false;

static inline bool is_ascii_alpha(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return (uc >= 'A' && uc <= 'Z') || (uc >= 'a' && uc <= 'z');
}

static inline bool is_ascii_digit(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return uc >= '0' && uc <= '9';
}

static std::string trim_ascii(std::string_view s) 
{
    std::size_t first = 0;
    while (first < s.size() &&
           std::isspace(static_cast<unsigned char>(s[first])) != 0) {
        ++first;
    }

    std::size_t last = s.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(s[last - 1])) != 0) {
        --last;
    }

    return std::string(s.substr(first, last - first));
}

static std::string ascii_lower(std::string_view s) 
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

static bool all_digits(std::string_view s) 
{
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!is_ascii_digit(c)) {
            return false;
        }
    }
    return true;
}

static std::vector<std::string_view> split_labels_sv(std::string_view s) 
{
    std::vector<std::string_view> labels;
    std::size_t start = 0;

    while (true) {
        const std::size_t dot = s.find('.', start);
        if (dot == std::string_view::npos) {
            labels.push_back(s.substr(start));
            break;
        }
        labels.push_back(s.substr(start, dot - start));
        start = dot + 1;
    }

    return labels;
}

static bool looks_like_ipv4(std::string_view host) 
{
    const auto labels = split_labels_sv(host);
    if (labels.size() != 4) {
        return false;
    }

    for (std::string_view label : labels) {
        if (label.empty() || label.size() > 3 || !all_digits(label)) {
            return false;
        }

        unsigned value = 0;
        for (char c : label) {
            value = value * 10u + static_cast<unsigned>(c - '0');
        }
        if (value > 255u) {
            return false;
        }
    }

    return true;
}

static bool is_valid_dns_label(std::string_view label,
                               bool allow_underscore = kAllowUnderscoreInLabels) 
{
    if (label.empty() || label.size() > 63) {
        return false;
    }

    auto ok = [allow_underscore](char c) {
        return is_ascii_alpha(c) || is_ascii_digit(c) ||
               c == '-' || (allow_underscore && c == '_');
    };

    if (!ok(static_cast<char>(label.front())) ||
        !ok(static_cast<char>(label.back()))) {
        return false;
    }

    if (label.front() == '-' || label.back() == '-') {
        return false;
    }

    for (char c : label) {
        if (!ok(c)) {
            return false;
        }
    }

    return true;
}

static bool validate_host_for_this_sorter(
    std::string_view host,
    bool allow_underscore = kAllowUnderscoreInLabels) 
{
    if (host.empty()) {
        return false;
    }

    // Conservative hostname-oriented bound.
    if (host.size() > 253) {
        return false;
    }

    // Must contain at least one dot so ordinary registrable domains match.
    if (host.find('.') == std::string_view::npos) {
        return false;
    }

    // Reject IP-looking and IPv6-ish inputs.
    if (host.find(':') != std::string_view::npos) {
        return false;
    }
    if (looks_like_ipv4(host)) {
        return false;
    }

    const auto labels = split_labels_sv(host);
    if (labels.size() < 2) {
        return false;
    }

    for (std::string_view label : labels) {
        if (!is_valid_dns_label(label, allow_underscore)) {
            return false;
        }
    }

    return true;
}

static std::optional<std::uint16_t> parse_port(std::string_view s) 
{
    if (!all_digits(s)) {
        return std::nullopt;
    }

    unsigned value = 0;
    for (char c : s) {
        value = value * 10u + static_cast<unsigned>(c - '0');
        if (value > 65535u) {
            return std::nullopt;
        }
    }

    return static_cast<std::uint16_t>(value);
}

static bool valid_scheme(std::string_view s) 
{
    if (s.empty() || !is_ascii_alpha(s.front())) {
        return false;
    }
    for (std::size_t i = 1; i < s.size(); ++i) {
        const char c = s[i];
        if (!(is_ascii_alpha(c) || is_ascii_digit(c) ||
              c == '+' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

static std::optional<ParsedHostPort> extract_host_port(std::string_view input) 
{
    const std::string s = trim_ascii(input);
    if (s.empty()) {
        return std::nullopt;
    }

    std::size_t authority_begin = 0;

    if (const std::size_t scheme_pos = s.find("://");
        scheme_pos != std::string::npos) {
        if (!valid_scheme(std::string_view(s.data(), scheme_pos))) {
            return std::nullopt;
        }
        authority_begin = scheme_pos + 3;
    } else if (s.rfind("//", 0) == 0) {
        authority_begin = 2;
    } else {
        // Lenient bare-authority mode:
        // parse up to / ? # as authority candidate even without scheme or //.
        authority_begin = 0;
    }

    std::size_t authority_end = s.find_first_of("/?#", authority_begin);
    if (authority_end == std::string::npos) {
        authority_end = s.size();
    }
    if (authority_end <= authority_begin) {
        return std::nullopt;
    }

    std::string_view authority(s.data() + authority_begin,
                               authority_end - authority_begin);

    const std::size_t first_at = authority.find('@');
    if (first_at != std::string_view::npos) {
        if (authority.find('@', first_at + 1) != std::string_view::npos) {
            return std::nullopt; // malformed multiple raw '@'
        }
        authority.remove_prefix(first_at + 1);
    }

    if (authority.empty()) {
        return std::nullopt;
    }

    // Reject bracketed IPv6/IP-literal.
    if (authority.front() == '[') {
        return std::nullopt;
    }

    const std::size_t colon = authority.rfind(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view host_sv = authority.substr(0, colon);
    std::string_view port_sv = authority.substr(colon + 1);

    if (host_sv.empty()) {
        return std::nullopt;
    }

    // More than one ':' in the host means IPv6-ish or malformed for our use.
    if (host_sv.find(':') != std::string_view::npos) {
        return std::nullopt;
    }

    auto port = parse_port(port_sv);
    if (!port) {
        return std::nullopt;
    }

    std::string host(host_sv);

    // Normalize a trailing FQDN dot away for grouping.
    if (!host.empty() && host.back() == '.') {
        host.pop_back();
    }

    host = ascii_lower(host);

    if (!validate_host_for_this_sorter(host)) {
        return std::nullopt;
    }

    return ParsedHostPort{std::move(host), *port};
}

static std::string reverse_labels(std::string_view domain) 
{
    const auto labels = split_labels_sv(domain);
    std::string out;

    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        if (!out.empty()) {
            out.push_back('.');
        }
        out.append(it->data(), it->size());
    }

    return out;
}

static std::string prefix_before_suffix(std::string_view host,
                                        std::string_view suffix) 
{
    if (host == suffix) {
        return {};
    }
    if (host.size() < suffix.size()) {
        return {};
    }

    const std::size_t start = host.size() - suffix.size();
    if (host.compare(start, suffix.size(), suffix) != 0) {
        return {};
    }
    if (start == 0 || host[start - 1] != '.') {
        return {};
    }

    return std::string(host.substr(0, start - 1));
}

static std::optional<std::string> heuristic_registrable_domain(std::string_view host) 
{
    const auto labels = split_labels_sv(host);
    if (labels.size() < 2) {
        return std::nullopt;
    }

    // Small heuristic for common 2-letter ccTLD structures.
    static const char* const common_second_level[] = {
        "ac", "co", "com", "edu", "gov", "net", "org"
    };

    auto is_common_second_level = [](std::string_view s) {
        for (const char* item : common_second_level) {
            if (s == item) {
                return true;
            }
        }
        return false;
    };

    bool use_three = false;
    if (labels.size() >= 3) {
        const std::string_view tld = labels[labels.size() - 1];
        const std::string_view sld = labels[labels.size() - 2];

        if (tld.size() == 2 &&
            sld.size() >= 2 && sld.size() <= 3 &&
            is_common_second_level(sld)) {
            use_three = true;
        }
    }

    const std::size_t take = use_three ? 3u : 2u;
    if (labels.size() < take) {
        return std::nullopt;
    }

    std::string out;
    for (std::size_t i = labels.size() - take; i < labels.size(); ++i) {
        if (!out.empty()) {
            out.push_back('.');
        }
        out.append(labels[i].data(), labels[i].size());
    }

    return out;
}

static std::optional<std::string> registrable_domain_from_psl(std::string_view host) 
{
    const psl_ctx_t* psl = psl_builtin();
    if (psl == nullptr) {
        return std::nullopt;
    }

    std::string host_copy(host);
    if (const auto normalized_host = normalize_host_with_libpsl(host); normalized_host) {
        host_copy = *normalized_host;
    }

    const char* reg = psl_registrable_domain(psl, host_copy.c_str());
    if (reg == nullptr) {
        return std::nullopt;
    }

    return std::string(reg);
}

static SortKey make_sort_key(const std::string& original) 
{
    SortKey key;
    key.original = original;

    const auto parsed = extract_host_port(original);
    if (!parsed) {
        return key;
    }

    key.port = parsed->port;
    key.normalized_host = parsed->host;

    auto registrable = registrable_domain_from_psl(parsed->host);
    if (!registrable) {
        registrable = heuristic_registrable_domain(parsed->host);
    }
    if (!registrable) {
        return key;
    }

    key.matched = true;
    key.registrable_domain = *registrable;
    key.registrable_reversed = reverse_labels(*registrable);
    key.subdomain_prefix = prefix_before_suffix(parsed->host, *registrable);
    return key;
}

bool sort_url_if_fit(const std::string& a, const std::string& b) 
{
    const SortKey ka = make_sort_key(a);
    const SortKey kb = make_sort_key(b);

    // matched strings first; then domain-aware grouping; then tie-breakers
    return std::make_tuple(
               !ka.matched,
               ka.registrable_reversed,
               ka.registrable_domain,
               ka.subdomain_prefix,
               ka.port,
               ka.normalized_host,
               ka.original)
         < std::make_tuple(
               !kb.matched,
               kb.registrable_reversed,
               kb.registrable_domain,
               kb.subdomain_prefix,
               kb.port,
               kb.normalized_host,
               kb.original);
}

} // namespace url_sort
