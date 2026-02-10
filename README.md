# ccdb

A Clash Dashboard in C++

![Nload](img/nload.png)

 - [Introduction](#introduction)
 - [Functionalities Provided](#functionalities-provided)
 - [Usage](#usage)
   * [Switch a Proxy Endpoint](#example-switch-proxy-in-a-proxy-group)
 - [How to Build](#how-to-build)

## Introduction

CCDB is intended as a lightweight C++ implementation of a clash dashboard
for devices that has extremely limited hardware resources (e.g., embedded devices),
or simply for users who don't want to allocate any over-the-top resources
for a simple dashboard.

CCDB is using multiple open-source libraries:
 - [CPP-HTTPLIB v0.30.2](https://github.com/yhirose/cpp-httplib)
 - [GNU Readline 8.3](https://ftp.gnu.org/gnu/readline/)
 - [GNU Ncurses 6.6](https://ftp.gnu.org/gnu/ncurses/)
 - [TSL Hopscotch-Hashing Map v2.4.0](https://github.com/Tessil/hopscotch-map)
 - [UTF8-CPP 4.0.9](https://github.com/nemtrif/utfcpp)
 - [JSON for Modern C++ 3.12.0](https://github.com/nlohmann/json)
 - [Perl 5.42.0](https://www.perl.org/)
 - [OpenSSL 3.6.1](https://github.com/openssl/openssl)

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
 - Pull Subscription usage info (You have to specify the subscription link in `~/.ccdbrc`)

***DORMANT FUNCTIONS***

 - Unicode proxy name parsing
 - Switch proxies inside pure a console with vector index
 - SSL Parsing

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
 ├ subinfo       : Subscription usage info
 ├ config        : Get current backend config in JSON
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
 │ ├ 12          : Log level
 │ ├ 13          : Log content
 ├ sort_reverse  : Reverse sorting
 │ ├ on
 │ ├ off
 ├ filter_reverse: Reverse filter
 │ ├ on
 │ ├ off
 ├ allowlan      : Allow LAN connections
 │ ├ on
 │ ├ off
 ├ loglevel      : Mihomo backend log level
 │ ├ silent
 │ ├ debug
 │ ├ info
 │ ├ warning
 │ ├ error
 ├ port          : Mihomo http proxy port
 ├ socksport     : Mihomo socks5 proxy port
 ├ redirport     : Mihomo redirect port
 ├ tproxyport    : Mihomo transparent proxy port
 ├ mixedport     : Mihomo mixed proxy port
close_connections: Close all connections
clear_filter     : Clear get connection filtering patterns
nload            : nload-like connection speed monitoring
reset            : Reset terminal mode
Environment:
   PAGER:    Specify a pager. Pager availability check is ignored when this environmental variable is set
   NOPAGER:  Set this to 'y' and force ccdb to ignore pager
   COLOR:    Set it to `never` to disable color codes
   JQ:       Set JSON parser, default is `jq`, if available
   TABSIZE:  Set tab size when printing tables, default is 4
   REVERSE_MOUSE: Reverse mouse scrolling direction when set to `true`
   NO_0xFE0F_EXPAND_EMOJI: Fix Unicode processing issues for emoji space expand code, e.g., "✈" and "✈️".
                           If you cannot notice any differences of the above emojis, or there's wierd Unicode processing bugs in your terminal,
                           you might want to set this to `true`
   DISABLE_SERVER_CERTIFICATE_VERIFICATION: When using `get subinfo`, SSL is enforced by default when link is https.
                                            Use this to skip server SSL certificate check.
   SSL_CERTIFICATE: When clash subscription link is in https, specify an SSL certificate when pulling subscription usage.
Keyboard Shortcuts:
  `get connections`: Get connections has multiple keyboard shortcuts:
     Mouse Click/Ctrl+UP/DOWN: Move highlight
                            K: Kill the highlighted connection
                            P: Print raw JSON from Mihomo core. If `jq` can be found, JSON will be parsed by jq
                       F1-F12: Specify which column (0-11) to sort the table, press on the same column again to reverse the sort
                       Ctrl+C: Abort the watcher
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

## WARNING

**This is free software; 
There is *NO* warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
If you choose CCDB in your production environment (which you should *NEVER EVER* do), 
you are on your own.**

**YOU HAVE BEEN WARNED**
