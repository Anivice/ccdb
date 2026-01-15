# ccdb

A Clash Dashboard in C++

![Nload](img/nload.png)

 - [Introduction](#introduction)
 - [Functionalities Provided](#functionalities-provided)
 - [Usage](#usage)
   * [Switch Proxy Endpoint](#example-switch-proxy-in-a-proxy-group)
 - [How to Obtain](#how-to-obtain)
 - [How to Build](#how-to-build)

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

## Functionalities Provided

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

CCDB is using dependencies that are published under GPL.
As a result, CCDB is licensed under GPLv3 as per dictated.

## Usage

Use `help` command to see usage details.

Currently, CCDB has the following usage:

```bash
help             : Show this help message
quit             : Quit the program
exit             : Quit the program
get              : Pull information
 ├ connections   : Active connections
 │ ├ hide        : Hide the column
 │ │ ├ 0         : Host
 │ │ ├ 1         : Process
 │ │ ├ 2         : DL
 │ │ ├ 3         : UP
 │ │ ├ 4         : DL Speed
 │ │ ├ 5         : UP Speed
 │ │ ├ 6         : Rules
 │ │ ├ 7         : Time
 │ │ ├ 8         : Source IP
 │ │ ├ 9         : Destination IP
 │ │ ├ 10        : Type
 │ │ ├ 11        : Chains
 │ ├ shot        : Use pager and pull the active connections once
 ├ latency       : Latency of an endpoint
 ├ proxy         : Proxy list
 ├ mode          : Proxy mode
 ├ log           : Active backend logs
 ├ vecGroupProxy : Vector proxy groups
 ├ filter        : Currently recorded filter
set              : Set parameters
 ├ mode          : Proxy mode
 │ ├ global      : Global mode
 │ ├ rule        : Rule mode
 │ ├ direct      : Direct mode
 ├ group         : Switch proxy group
 │ ├ [GROUP]     : Group name
 │ ├ [PROXY]     : Proxy endpoint name
 ├ vgroup        : Vector group
 │ ├ [VGROUP]    : Group vector number
 │ ├ [VPROXY]    : Proxy vector number
 ├ chain_parser  : Chain parser
 │ ├ on
 │ ├ off
 ├ sort_by       : Sort by column
 │ ├ 0           : Host
 │ ├ 1           : Process
 │ ├ 2           : DL
 │ ├ 3           : UP
 │ ├ 4           : DL Speed
 │ ├ 5           : UP Speed
 │ ├ 6           : Rules
 │ ├ 7           : Time
 │ ├ 8           : Source IP
 │ ├ 9           : Destination IP
 │ ├ 10          : Type
 │ ├ 11          : Chains
 ├ filter        : get connections filter pattern
 │ ├ 0           : Host
 │ ├ 1           : Process
 │ ├ 6           : Rules
 │ ├ 8           : Source IP
 │ ├ 9           : Destination IP
 │ ├ 10          : Type
 │ ├ 11          : Chains
 ├ sort_reverse  : Reverse sorting
 │ ├ on
 │ ├ off
 ├ filter_reverse: Reverse filter
 │ ├ on
 │ ├ off
close_connections: Close all connections
clear_filter     : Clear get connection filtering patterns
nload            : nload-like connection speed monitoring
Environment:
   PAGER:    Specify a pager. Pager availability check is ignored when this environmental variable is set
   NOPAGER:  Set this to 'y' and force ccdb to ignore pager
   COLOR:    Set it to `never` to disable color codes
   NO_0xFE0F_EXPAND_EMOJI: Fix Unicode processing issues for emoji space expand code, e.g., "✈" and "✈️".
                           If you cannot notice any differences of the above emojis, or there's wierd Unicode processing bugs in your terminal,
                           you might want to set this to `true`
```

Use double Tab to list possible candidates in a command,
this will list all the available candidates like proxy endpoints or groups,
with additional information like latency (if tested) and vector index.

### Example: Switch Proxy in a Proxy Group

#### Step 1

Select the group you want to switch by the following command:

```bash
    set vgroup [DOUBLE TAB TO LIST GROUPS YOU WANT TO SWITCH]
```

You should see the vector results similar to this image (groups and endpoints are redacted):

![Image](img/set_vgroup.png)

#### Step 2

Say, I want to change group `58`.
Use the following command to list the proxy I can set for this group:

```bash
    set vgroup 58 [DOUBLE TAB TO LIST WHICH PROXY ENDPOINT YOU WANT TO SWITCH TO]
```

![Image](img/set_vgroup2.png)

> As is shown in the above image, currently selected proxy is marked with a prefix " * ".
> In the above image, the selected endpoint for proxy group `58` is `5`.

#### Additional Note

If you tested latency before using `get latency`,
the latency results will be shown in the parentheses, i.e., `([\d]+)`,
as is shown in the following image.

![Image](img/set_vgroup3.png)

## How to Obtain

### Linux

You can download pre-built binaries from
[the release page](https://github.com/Anivice/ccdb/releases)
(well bye-bye free 2k minutes per months),
or use the command

```bash
    curl -fsSL "https://raw.githubusercontent.com/Anivice/ccdb/refs/heads/main/src/script/update.sh" | bash -s -- [DESTINATION]
```

to download the latest executable to the location you want to download to from the release page automatically.

You need `wget` and `git` for the script to work properly.

### Windows

CCDB runs on [Cygwin](https://www.cygwin.com/) on Windows.
You can obtain the cygwin build from [the release page](https://github.com/Anivice/ccdb/releases) published by GitHub Actions.
Tags for Cygwin build is `ccdb.cygwin.NightlyBuild.[FIST 8 CHARACTERS OF GIT COMMIT HASH]`.

Since GitHub tags are very unorganized, you can use the following script
to directly pull the latest executable from the release page.
You have to execute this script under Cygwin environment.

```bash
    curl -fsSL "https://raw.githubusercontent.com/Anivice/ccdb/refs/heads/main/src/script/update_cygwin.sh" | bash -s -- [DESTINATION]
```

Again, you need `wget` and `git` for the script to work properly.

Currently, all builds are nightly builds marked as pre-release.
No "stable" build has been released, yet.

### Self update

You can use the command

```bash
   ccdb update
```

to update the program from the GitHub directly.
This will spawn an orphaned subprocess to update the executable when possible.

Be sure to kill all `ccdb` process before updating.

However, DO BE ALARMED that this updater assumed a stable network environment
and is by no means atomic, which CAN BREAK the existing file
when the updater cannot correctly pull the desired content from GitHub.
Use this at your own risk.

If you ran into an issue when updating and the original `ccdb` is damaged,
you can resume the update process by the methods mentioned above.

## How to Build

### Linux

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
to automatically build for toolchain-supported architecture.
Currently, ccdb has the toolchains embedded for the following architectures:

  - *ARM*
  - *ARMv5l*
  - *ARMv7hf*
  - *ARMv7*
  - *i586*
  - *i686*
  - AARCH64
  - x86_64

> Architectures in cursive are 32bit architectures that `CPP-HTTPLIB` has already
> announced as **deprecated**.
> The executable runs as expected but vulnerabilities and BUGs might present in these executables
> as they do not receive any test apart from a simple functionality check.
> ***If you choose to use these executables, you are on your own.***
> You have been warned.

### Windows

Use the command

```bash
  git clone https://github.com/Anivice/ccdb && cd ccdb && mkdir build && cd build && cmake ../src/ -DCCDB_CYGWIN_BUILD=True && make
```
to build locally.

x86_64 is the only architecture ever tested. The build system must live under cygwin.
Debug builds for Windows are not supported.

## WARNING

**This is free software; 
There is *NO* warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
If you choose CCDB in your production environment (which you should *NEVER EVER* do), 
you are on your own.**

**YOU HAVE BEEN WARNED**
