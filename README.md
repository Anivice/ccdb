# CCDB

A lightweight terminal dashboard for Clash/Mihomo, written in C++.

![Nload](img/nload.png)

 - [Overview](#Overview)
 - [Features](#Features)
 - [Usage](#usage)
 - Quick Start Examples:
   * [Example 1: Switch Proxy in a Proxy Group](#example-1-switch-proxy-in-a-proxy-group)
   * [Example 2: Shell Parsing of `get config`](#Example-2-Shell-Parsing-of-get-config)
 - [How to Build](#how-to-build)

## Overview

CCDB targets low-resource environments (for example, embedded devices) and users who want a low-overhead dashboard.

CCDB depends on the following open-source libraries:
 - [CPP-HTTPLIB v0.37.0](https://github.com/yhirose/cpp-httplib)
 - [GNU Readline 8.3](https://ftp.gnu.org/gnu/readline/)
 - [GNU Ncurses 6.6](https://ftp.gnu.org/gnu/ncurses/)c
 - [TSL Hopscotch-Hashing Map v2.4.0](https://github.com/Tessil/hopscotch-map)
 - [UTF8-CPP 4.0.9](https://github.com/nemtrif/utfcpp)
 - [JSON for Modern C++ 3.12.0](https://github.com/nlohmann/json)
 - [Perl 5.42.0](https://www.perl.org/)
 - [OpenSSL 3.6.1](https://github.com/openssl/openssl)

## Features

### CCDB provides

 - Immediately close all connections
 - Watch currently active connections
 - nload-like traffic updates 
 - Switch the proxy mode (between "direct", "global", and "rule")
 - Watch Mihomo backend logs
 - Test latencies
 - Pull Subscription usage info (You have to specify the subscription link in `~/.ccdbrc`)
 - Unicode proxy name handling (experimental)
 - Switch proxies in a pure console using numeric indices
 - SSL Parsing
 - Shell parsing of ccdb command output

> **Additional notes for vector index**:
> **The console needs to be able to actually show these characters**,
> **otherwise** you might see placeholder glyphs, which makes the indices impossible to match to names.
> There are many ways to achieve this,
> but the simplest is using a bare-metal X display server like Xorg or XLibre.

CCDB is distributed under the GPLv3.
See LICENSE for details.
Some dependencies are licensed under the GPL.

## Usage

Use `help` command to see usage details.

Syntax:

```bash
./ccdb [Arguments [OPTIONS...]...]
    -h,--help                Show help
    -v,--version             Show version
    -u,--url [ARG]           Backend url, usually http://localhost:9090
    -x,--execute [ARG]       Execute a CCDB command
    -t,--token [ARG]         Backend HTTP auth password
    -l,--latency_url [ARG]   Latency URL
```

**Frequently Used Commands**:

```bash
...
get connections                 : Pull Active connections
close_connections               : Close all connections
nload                           : nload-like connection speed monitoring
set mode [global, rule, direct] : Change proxy mode
set vgroup [VGROUP] [VPROXY]    : Change endpoints in a proxy group using index
...
```

**Environment**:

| Environment Variable                    | Descriptions                                                                                                                                                                                                                                                                             |
|-----------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| PAGER                                   | Specify a pager. Pager availability check is ignored when this environmental variable is set                                                                                                                                                                                             |
| NOPAGER                                 | Set this to 'y' and force ccdb to ignore pager                                                                                                                                                                                                                                           |
| COLOR                                   | Set it to `never` to disable color codes                                                                                                                                                                                                                                                 |
| JQ                                      | Set JSON parser, default is `jq`, if available                                                                                                                                                                                                                                           |
| TABSIZE                                 | Set tab size when printing tables, default is 4                                                                                                                                                                                                                                          |
| REVERSE_MOUSE                           | Reverse mouse scrolling direction when set to `true`                                                                                                                                                                                                                                     |
| NO_0xFE0F_EXPAND_EMOJI                  | Fix Unicode processing issues for emoji space expand code, e.g., "✈" and "✈️".                         If you cannot notice any differences of the above emojis, or there's wierd Unicode processing bugs in your terminal,                         you might want to set this to `true` |
| DISABLE_SERVER_CERTIFICATE_VERIFICATION | When using `get subinfo`, TLS is enabled by default when the subscription URL uses HTTPS.                                          Use this to skip server SSL certificate check.                                                                                                        |
| SSL_CERTIFICATE                         | When the Clash subscription link is in https, specify an SSL certificate when pulling subscription usage.                                                                                                                                                                                |

**Keyboard Shortcuts**:

  - `get connections`: Get connections has multiple keyboard shortcuts:
      * Mouse Click/Ctrl+UP/DOWN: Move highlight
      * K: Kill the highlighted connection
      * P: Print raw JSON from Mihomo core. If `jq` can be found, JSON will be parsed by jq
      * F1-F12: Specify which column (0-11) to sort the table, press on the same column again to reverse the sort
      * Ctrl+C: Abort the watcher

Press Tab twice to list candidates in a command,
this will list all the available candidates like proxy endpoints or groups,
with additional information like latency (if tested) and vector index.

To run a shell command, prefix it with `"$ "` (dollar sign + space/`$[WITH A SPACE]`).

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

### Example 2: Shell Parsing of `get config`

CCDB supports shell parsing of its own command outputs.
Unlike shells, "|" has to be surrounded by spaces, i.e., ` | ` (`[SPACE]|[SPACE]`).
This can be useful for commands like `get config` to parse JSON on external utilities like `jq`.
For example, you can select `dns-hijack` from the JSON reply from backend using 
`get config | jq -r '.tun["dns-hijack"]'`:

![Image](img/ccdb_get_config_jq.png)

Or, you can even pipe a POSIX script into the pipeline:

![Image](img/script_pipe.png)

As you can see, when executing a piped script, you can invoke `ccdb` with
environment variable `$CCDB`, which is a duplicate of the parent `ccdb` arguments.

## How to Build

### Linux

Use the command

```bash
  git clone https://github.com/Anivice/ccdb && cd ccdb && mkdir build && cd build && cmake ../src/ && make
```

to build locally.

#### CMake Settings
  * OPENSSL_TARGET: Supported openssl target, if you are cross-compiling you need to specify a platform.
  * OPENSSL_LIBP: Depending on the target, openssl can have its lib in `.../lib` or `.../lib64`, you can tell CMake this info by setting it to `lib` or `lib64`.

## Disclaimer

**This is free software; 
There is *NO* warranty;
not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
This project is provided "as is". DO NOT use it in production.**

**YOU HAVE BEEN WARNED**
