# Lyra Package Manager (v0.2) MIT

Lyra is a lightweight, JSON-based package manager built in C that uses cJSON for structured metadata storage.
It manages local binary packages, keeps version history, and maintains a full backup system — all stored neatly inside the user’s home directory.

# Overview

Lyra installs precompiled binaries from URLs (usually .tar.gz archives), automatically extracts them, moves them to /usr/local/bin, and tracks them in a JSON database.

Each installed package can:
Be muted (reverted) to a previous version.
Be removed cleanly.
Be listed with full version and backup info.

All package information, including previous versions and metadata, is stored under:
```
 ~/.lyra/
├── active_packages.json   # database of current & muted packages
└── vault/                 # backup copies of binaries per version
```

current documentation is in:
[Lyra Package Manager Docs](https://docs.google.com/document/d/1OVEcteiQob15ftbCBXE5kjshAIQ0-OPxq-sOjcwaShY/edit?usp=sharing)
