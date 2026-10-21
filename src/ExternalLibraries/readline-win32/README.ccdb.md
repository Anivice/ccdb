# Vendored native GNU Readline

GNU Readline 8.2 release archive:
https://mirrors.kernel.org/gnu/readline/readline-8.2.tar.gz

The `readline/` sources include their upstream COPYING and documentation.
The Windows glue and `upstream-win32.patch` come from
https://github.com/chrisant996/readline_win32_sample at
`e0fc03409ace6dd44299d302693c8a6a44e94577`.
The integration license is retained in `COPYING.integration`.

`upstream-win32.patch` is applied to the release source, rather than to the
separate Readline 8.3 source used by the Linux build. CMake replaces the sample's
Premake build. Local modifications and validation are recorded in the root
RESUME.md. The sample is an integration reference, not an upstream production
support guarantee.
