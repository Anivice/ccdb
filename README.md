# CCDB

[![CI](https://github.com/Anivice/ccdb/actions/workflows/LinuxBuildAction.yml/badge.svg?branch=main)](https://github.com/Anivice/ccdb/actions/workflows/LinuxStaticBuildAction.yml)
[![CI](https://github.com/Anivice/ccdb/actions/workflows/LinuxBuildActionAARCH64.yml/badge.svg?branch=main)](https://github.com/Anivice/ccdb/actions/workflows/LinuxStaticBuildAction.yml)

> [!WARNING]
> ***CCDB HAS POOR CHINESE LANGUAGE SUPPORT***
>
> The translation is incomplete and existing translations are a mess.
> Texts lacking Chinese corresponding translations are shown in English.
> Missing texts will be shown in `~/.config/ccdb/MISSING-TRANSLATIONS.json` for Release Candidates.

A lightweight terminal dashboard for Clash/Mihomo, written in C++.

![Nload](img/nload.png)

 - [Overview](#Overview)
 - [Usage](#usage)
 - Quick Start Examples:
   * [Example 1: Switch Proxy in a Proxy Group](#example-1-switch-proxy-in-a-proxy-group)
   * [Example 2: Map the Entire Proxy Chain](#example-2-map-the-entire-proxy-chain)
   * [Example 3: Shell Parsing of `get config`](#Example-3-Shell-Parsing-of-get-config)
   * [Example 4: Dump backend debug info](#Example-4-Dump-backend-debug-info)
 - [How to Build](#how-to-build)

## Overview

CCDB targets low-resource environments (for example, embedded devices) and users who want a low-overhead dashboard.

CCDB depends on the following open-source libraries:
 - [CPP-HTTPLIB v0.53.1](https://github.com/yhirose/cpp-httplib) (Embedded)
 - [GNU Readline 8.3](https://ftp.gnu.org/gnu/readline/) (Embedded)
 - [GNU Ncurses 6.6](https://ftp.gnu.org/gnu/ncurses/) (Embedded)
 - [TSL Hopscotch-Hashing Map v2.4.0](https://github.com/Tessil/hopscotch-map) (Embedded)
 - [UTF8-CPP 4.2.0](https://github.com/nemtrif/utfcpp) (Embedded)
 - [JSON for Modern C++ 3.12.0](https://github.com/nlohmann/json) (Embedded)
 - [Perl 5.42.0](https://www.perl.org/) (Not embedded, required by OpenSSL)
 - [OpenSSL 3.6.2](https://github.com/openssl/openssl) (Embedded)
 - [GNU Tar 1.35](https://www.gnu.org/software/tar) (Embedded)
 - [XXD, from VIM](https://github.com/vim/vim/) (Not embedded, required by the build system)
 - [libpsl, 0.23.3](https://github.com/rockdaboot/libpsl/) (Embedded)
 - [zlib 1.3.2](https://www.zlib.net/) (Embedded)
 - [libpng 1.6.58](https://www.libpng.org/pub/png/libpng.html) (Embedded)
 - [stb_image - v2.30 - public domain image loader](http://nothings.org/stb)
 - [CImg 3.7.5](https://www.cimg.eu/) (Embedded)
 - libtiv - Copyright © 2017-2023, Stefan Haustein, Aaron Liu (Embedded)
 - [Abseil - C++ Common Libraries 20260526.0](https://github.com/abseil/abseil-cpp/) (Embedded, required by RE2)
 - [RE2, a regular expression library 2025-11-05](https://github.com/google/re2) (Embedded)

## Usage

Use `help` command to see usage details.

Press Tab twice to list candidates in a command,
this will list all the available candidates like proxy endpoints or groups,
with additional information like latency (if tested) and vector index.

To run a shell command, prefix it with `"$ "` (dollar sign + space/`$[WITH A SPACE]`).

> **NOTE 1:**
> 
> Double tab will show the helper details of each and every candidate:
> 
> ![img](img/helper.png)
> ![img](img/helper2.png)

> **NOTE 2:**
> 
> https://github.com/Anivice/ccdb/blob/c5b70b0a5202ae794469e36ee95c3f00c646facf/ccdbrc.example#L56-L57
> 
> Comments for the aliases will be noted and printed in the double tab helper as well:
> 
> e.g.:
> 
> ~/.ccdbrc definition: ![~/.ccdbrc definition](img/helper4.png)
> 
> Helper output: ![Helper output](img/helper3.png)


### Example 1: Switch Proxy in a Proxy Group

#### Step 1

Select the group you want to switch by the following command:

```bash
set vgroup [DOUBLE TAB TO LIST GROUPS YOU WANT TO SWITCH]
```

You should see the vector results similar to this image (groups and endpoints are redacted):

![Image](img/set_vgroup.png)

#### Step 2

Say, I want to change group `44`.
Use the following command to list the proxy I can set for this group:

```bash
set vgroup 44 [DOUBLE TAB TO LIST WHICH PROXY ENDPOINT YOU WANT TO SWITCH TO]
```

![Image](img/set_vgroup2.png)

> As is shown in the above image, currently selected proxy is marked with a prefix " * "
> and is highlighted in the candidate selection prompt.
> The selected endpoint for the above proxy group `44` is `3`.

#### Additional Note

If you tested latency before using `get latency`,
the latency results will be shown in the parentheses, i.e., `([\d]+)`,
as is shown in the following image.

![Image](img/set_vgroup3.png)

### Example 2: Map the Entire Proxy Chain

![Image](img/mapProxyChains.png)

### Example 3: Shell Parsing of `get config`

CCDB supports shell parsing of its own command outputs.
This can be useful for commands like `get config` to parse JSON on external utilities like `jq`.
For example, you can select `dns-hijack` from the JSON reply from backend using 
`get config | jq -r '.tun["dns-hijack"]'`:

![Image](img/ccdb_get_config_jq.png)

Or, you can even pipe a POSIX script into the pipeline:

![Image](img/script_pipe.png)

As you can see, when executing a piped script, you can invoke `ccdb` with
environment variable `$CCDB`, which is a duplicate of the parent `ccdb` arguments.

### Example 4: Dump backend debug info

Launch `ccdb` with extra features enabled using the command `CCDB_ENABLE_EXPERIMENTAL_FEATURES=true ccdb`.

```bash
   ccdb> get memory pprof heap > /tmp/heap
```

Here we dumped backend heap file to `/tmp/heap`.

## How to Build

### Linux

#### Requirements

You need

  * a complete C++ toolchain of either GCC or Clang that can process at least C++ 17 (GCC >= 13 would be fine)
  * `cmake` and your build system of choice (either Unix Makefile [`make`] or Ninja [`ninja`]).
  * `tar`, `sed`, and `grep`, the standard POSIX tools that should exist on all Linux systems by default.
  * GNU's `automake`, required by `tar` package for `ccdb`

to build CCDB.

All dependencies are embedded inside the source code. No additional installation required.

#### CMake Settings
  * `OPENSSL_TARGET`: Supported openssl target, if you are cross-compiling you need to specify a platform.
  * `OPENSSL_LIBP`: Depending on the target, openssl can have its lib in `.../lib` or `.../lib64`, you can tell CMake this info by setting it to `lib` or `lib64`.

#### Build

On x86_64, you can use the command

```bash
  git clone https://github.com/Anivice/ccdb --depth=1 && \
  cd ccdb && mkdir build && cd build && \
  cmake ../src/ -DPERL_MAKE_ADDITIONAL_FLAGS=-j$(nproc) -DOPENSSL_MAKE_ADDITIONAL_FLAGS=-j$(nproc) -DREADLINE_MAKE_ADDITIONAL_FLAGS=-j$(nproc) -DNCURSES_MAKE_ADDITIONAL_FLAGS=-j$(nproc) \
      -DCC_ADDITIONAL_OPTIONS="-O3 -fomit-frame-pointer -ffast-math -fstrict-aliasing -fdata-sections -ffunction-sections -D_FORTIFY_SOURCE=2 -fno-stack-protector -s" \
      -DLD_ADDITIONAL_OPTIONS="-O3 -fomit-frame-pointer -ffast-math -fstrict-aliasing -fdata-sections -ffunction-sections -D_FORTIFY_SOURCE=2 -fno-stack-protector -s" \
      -DOPENSSL_TARGET=linux-x86_64 -DOPENSSL_LIBP=lib64 && \
  make -j$(nproc)
```

to build locally.

## Disclaimer

**This is free software; 
There is *NO* warranty;
not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This project is provided "as is." DO NOT use it in production.**

**YOU HAVE BEEN WARNED**

**CCDB is GPLv3-licensed to comply with its statically linked GPL dependencies.**

See `LICENSE` for details.
