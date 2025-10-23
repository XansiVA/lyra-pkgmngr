# Lyra Package Manager (v0.7)
Copyright (c) 2025 Xansi
Licensed under the GPL v3.0 License (see LICENSE file)

Lyra is a lightweight, JSON-based package manager built in C that uses cJSON for structured metadata storage.
It manages local binary packages, keeps version history, and maintains a full backup system — all stored neatly inside the user’s home directory.

# Overview

Lyra installs precompiled binaries from URLs (usually .tar.gz archives), automatically extracts them, moves them to /usr/local/bin, and tracks them in a JSON database.

Each installed package can:
Be muted (reverted) to a previous version.
Be removed cleanly.
Be listed with full version and backup info.
others are included in the documentation;

**WARNING**
This is still in early development age, may not currently work completely as expected, please send bugs to a pull request.

All package information, including previous versions and metadata, is stored under:
```
 ~/.lyra/
├── active_packages.json   # database of current & muted packages
└── vault/                 # backup copies of binaries per version
```

Usable commands currently are:
```
  lyra -i <package> <url>               Install package (auto-mutes old version)
  lyra -rmpkg <pkg1> [pkg2] [pkg3]...  Remove packages (keeps vault copies)
  lyra -rmcpkg <pkg1> [pkg2] ...       Remove packages completely (deletes vault)
  lyra -list                            List installed packages
  lyra -lv <package>                    List all versions of a package
  lyra -m <package>                     Cycle to next muted version
  lyra -m <package@version>             Switch to specific version
  lyra -um <package>                    Unmute package (reactivate muted version)
  lyra -ss                              Take system snapshot
  lyra -ssl                             List all snapshots
  lyra -rsw <date> [number]             Restore snapshot (DD-MM-YYYY)
  lyra -U                               Update packages (GitHub or mirror)
  lyra -clean                           NUCLEAR: Delete everything and reset
```

# HOW TO USE.
Currently, there is no version where lyra can pull from a repository, due to lack there of the following:
Servers to host packages with,
or a completely functioning package manager to pull from them.

To currently use the package manager you must first:
compile using `gcc lyra.c -o lyra -lcjson`, then install a tarball with usability from a git repository I prefer using ripgrep as following.
using two versions so you can test.

```
./lyra -i rg https://github.com/BurntSushi/ripgrep/releases/download/14.1.0/ripgrep-14.1.0-x86_64-unknown-linux-musl.tar.gz
./lyra -i rg https://github.com/BurntSushi/ripgrep/releases/download/13.0.0/ripgrep-13.0.0-x86_64-unknown-linux-musl.tar.gz
```

now test any of the usable commands that are listed.

Refer to [Lyra Package Manager Docs](https://docs.google.com/document/d/1OVEcteiQob15ftbCBXE5kjshAIQ0-OPxq-sOjcwaShY/edit?usp=sharing) for anything you do not understand or see.
