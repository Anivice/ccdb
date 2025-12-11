# ccdb

A C++ Clash Dashboard in C++ With GNU ncurses and readline

## Introduction

I'm just ganna say it, and it's coming right out:

**Clash Verge Rev and ALL of its derivatives are BLOATED AS FUCK.**

FUCK WEBSLOPS, like, seriously.
You need ~600 to ~700 MB for ONE APP whose functionalities are 99.99% unwanted.
It provides absolutely **ZERO CORE** functions that you would use because Mihomo core
single-handedly handles that on its own, including Tun, already.
They are nothing but frontend UIs.

> I've seen enough people posting "unused RAM is wasted RAM."
> Now, this would be my response if I were to be sarcastic
> or just intended as a joke thing to say.
> This would be true, if I am on fucking **DOS**.
> But, NO.
> You are in a modern operating system whose resources are supposed to be **SHARED**,
> you don't, **NEVER**, claim ***ALL* THE AVAILABLE RESOURCES INSIDE A SYSTEM TO YOUR OWN**.

**Clash Verge Rev asks for *ABSOLUTELY CRAZY* security clearance level**

This is the part that really pissed me off. WHAT THE ACTUAL FUCK?
So you are saying, I've already installed the package using `sudo`,
but no Tun-supporting systemd service for Mihomo by default.

Are you fucking kidding me? Was it so fucking hard?

I mean, ok, fine, it's not like Mihomo can't run.
Then you ask me for **root access** ***AFTER*** the installation saying that
I "need to 'install (the) service' before using Tun."
You didn't think about doing that in the fucking package,
you need to ask for root permission to execute **GOD KNOWS WHAT** *after* the installation?
Like, who the fuck knows what you did?
Maybe you just reported me to the government, I don't fucking know,
it's not like the package I downloaded is 100% untainted
because it's not released by GitHub Actions.
Who knows what the fuck you put in.

Thus, CCDB, the C++ Clash Dashboard. You need only ~6MB to run the whole thing
(or even less, depending on the architectures),
and you don't have to install NPM or Rust or WebView or any of the slopwares, AT ALL.
You can just drag this thing into your server or embedded devices like RPi3,
without X11, SSH-X11forwarding, VNC, Wayland Compositors,
or any other resource consuming services that
doesn't really justify the installation in a pure console environment,
or basically anywhere.

## Functionalities provided

### CCDB provides

***ACTIVE FUNCTIONS***

 - Immediately close all connections
 - Watch currently active connections
 - Nload-like traffic updates 
 - Switch the proxy mode (between "direct", "global", and "rule")
 - Switch a proxy for a proxy group
 - Watch Mihomo backend logs
 - Test latencies

***DORMANT FUNCTIONS***

 - Unicode name parsing (experimental)
 - Switch proxies inside pure console (no Chinese input methods available, no mouse cursor)

> **Additional notes for that last part**:
> You need **your console to actually be able to show these characters** though,
> **otherwise** it'd be just **blocks all over the screen** with indexes
> that you have no possible reference to,
> because they are all, as I have said already, blocks.

That's it, no BS, just all the core functions that people actually care and touch.

CCDB is using GNU readline 8.3 and GNU ncurses 6.5.
As a result, CCDB is licensed under GPLv3 as per dictated.

## Usage

Use "help" command to see usage details.

Use double Tab to list possible candidates in a command.

## How to build

You can download pre-built binaries from the [release page](https://github.com/Anivice/ccdb/releases)
(well bye-bye free 2k minutes per months),
or use the command

```bash
  git clone https://github.com/Anivice/ccdb && cd ccdb && mkdir build && cd build && cmake ../src/ && make
```

to build locally.

If you are on x86, you can actually use the embedded toolchains
to build fully statically-linked, self-contained executables
(that are already published per-git on GitHub by GitHub Actions)
locally, or use script helper as
`src/configure.sh [ARCH] /tmp/build_[ARCH]`
(e.g., `src/configure.sh aarch64 /tmp/build_aarch64`)
to automatically build for toolchain-supported architectire.
