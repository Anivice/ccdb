SRELL 2026.07 (17 September 2026), BSD-2-Clause.

Unmodified single-header distribution from
https://akenotsuki.pages.dev/misc/srell/releases/srell2026_07.zip

Archive SHA-256: `d384feae20c66b4d65bc240ee2a851aed26ca24dc4d73503eee3e81e238ec20e`.

Used for Unicode regex matching because portable std::regex support is limited
to char and wchar_t, and wchar_t is only 16 bits on Windows. Existing char-based
replacement calls retain std::regex semantics; Unicode strings use SRELL.
