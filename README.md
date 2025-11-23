# ccdb

C++ Clash Dashboard in ncurses

## Introduction

**Clash Verge Rev is BLOATED AS FUCK.**

You need 600 MB for one app whose functionalities are 99% unwanted.
It provides absolutely ZERO core functions that you would use because Mihomo core
single-handedly handles that on its own, including Tun.

**Clash Verge Rev asks CRAZY security clearance level**

You already installed a systemd service, and for some reason that systemd is not for Mihomo.
I mean, ok, fine. But it asks for root access AFTER its installation finished saying that
"You need to 'install service' before using Tun."
The fuck? You didn't do that in the package,
you need to ask for root permission AGAIN after installation?
What the actual fuck?

Thus, CCDB, C++ Clash Dashboard. You need only ~60MB to run the whole thing,
and you don't have to install NPM or Rust or WebView, AT ALL.
Two more things that absolutely nobody wants.

## Functionalities provided

CCDB provides

 - Close All Connections
 - Watch Current Active Connections
 - Switch Proxy Mode ("Direct", "Global", "Rule")
 - Switch Proxy Inside a Group
 - Watch Logs
 - Test Latencies

That's it, no BS, just all the core functions that people actually use.

CCDB is using GNU readline 8.3 and GNU ncurses 6.5.
As a result, CCDB is licensed under GPLv3.

**NOTE:** Use double Tab to list possible candidates in a command.
Use "help" command to see details of a sub command.

## How to build

You can download pre-built binaries from the release page, or use the command

```bash
https://github.com/Anivice/ccdb && cd ccdb && mkdir build && cd build && cmake ../src/ && make
```

to build locally
