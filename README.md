# ccdb

A Clash Dashboard in C++

![Nload](img/nload.png)

## Introduction

CCDB is intended as a lightweight C++ implementation of a clash dashboard
for devices that has extremely limited hardware resources (e.g., embedded devices),
or simply for users who don't want to allocate any over-the-top resources
for a simple dashboard.

CCDB is using multiple open-source libraries:
 - [CPP-HTTPLIB v0.30.1](https://github.com/yhirose/cpp-httplib)
 - [GNU Readline 8.3](https://ftp.gnu.org/gnu/readline/)
 - [GNU Ncurses 6.6](https://ftp.gnu.org/gnu/ncurses/)
 - [TSL Hopscotch-Hashing Map v2.4.0](https://github.com/Tessil/hopscotch-map)
 - [UTF8-CPP 4.0.9](https://github.com/nemtrif/utfcpp)
 - [JSON for Modern C++ 3.12.0](https://github.com/nlohmann/json)

## Functionalities provided

### CCDB provides

***ACTIVE FUNCTIONS***

 - Immediately close all connections
 - Watch currently active connections
 - nload-like traffic updates 
 - Switch the proxy mode (between "direct", "global", and "rule")
 - Switch a proxy for a proxy group (with vector numbers)
 - Watch Mihomo backend logs
 - Test latencies

***DORMANT FUNCTIONS***

 - Unicode proxy name parsing (experimental)
 - Switch proxies inside pure a console with vector index

> **Additional notes for vector index**:
> **The console needs to be able to actually show these characters** though,
> **otherwise** it'd be just **blocks all over the screen** with indexes
> that have no possible references to know what each of them means.
> There are many ways to achieve this,
> but the simplest is using a bare-metal X display server like Xorg or XLibre.

CCDB is using dependencies that is published under GPL.
As a result, CCDB is licensed under GPLv3 as per dictated.

## Usage

Use `help` command to see usage details.

Use double Tab to list possible candidates in a command.

### Switch Proxy in a Proxy Group

Select the group you want to switch by the following command:

```bash
    set vgroup [DOUBLE TAB TO LIST GROUPS YOU WANT TO SWITCH]
```

You should see the vector results similar to this image (groups and endpoints are redacted):

![Image](img/set_vgroup.png)

Say, I want to change group `58`.
Use the following command to list the proxy I can set for this group:

```bash
    set vgroup 58 [DOUBLE TAB TO LIST WHICH PROXY ENDPOINT YOU WANT TO SWITCH TO]
```

![Image](img/set_vgroup2.png)

If you tested latency before using `get latency`,
the latency results will be shown in the parentheses, i.e., `([\d]+)`,
as is shown in the following image.

![Image](img/set_vgroup3.png)

## How to Obtain

You can download pre-built binaries from
[the release page](https://github.com/Anivice/ccdb/releases)
(well bye-bye free 2k minutes per months),
or use the command

```bash
    curl -fsSL "https://raw.githubusercontent.com/Anivice/ccdb/refs/heads/main/src/script/update.sh" | bash -s -- [DESTINATION]
```

to download the latest executable to the location you want to download to from the release page automatically.

You need `wget` and `git` for the script to work properly.

## How to Build

Use the command

```bash
  git clone https://github.com/Anivice/ccdb && cd ccdb && mkdir build && cd build && cmake ../src/ && make
```

to build locally.

If you are on x86, you can actually use the embedded toolchains
to build fully statically-linked, self-contained executables
(that are already published per-git-commit on GitHub by GitHub Actions automatically) locally
with `configure.sh` as `src/configure.sh [ARCH] [BUILD TEMP DIR]`
(e.g., `src/configure.sh aarch64 /tmp/build_aarch64`)
to automatically build for toolchain-supported architectire.
Currently, ccdb has the toolchains embedded for the following architectures:

  - ARM
  - ARMv5l
  - ARMv7hf
  - ARMv7
  - i586
  - i686
  - AARCH64
  - x86_64

> ***WARNINGS***:
> 
> 1. CPP-HTTPLIB doesn't support 32bit CPUs but so far it seems to be working as intended.
> However, vulnerabilities or exploitative behaviors might present in these executables since they
> do not receive any human oversight to any degree.
> 
> 2. **This is free software;
> There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
> If you choose CCDB in production environment (which you should *NEVER* do), you are on your own.** 
