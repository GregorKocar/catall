# catall

catall is a recursive code and text export utility written in C++ for Linux.

It recursively scans directories and concatenates source and text files into a single readable output stream. It is designed for:

- AI context export
- source code reviews
- backups
- debugging
- code analysis
- documentation generation
- terminal-based code inspection

## Features

- Recursive directory scanning
- Configurable file headers
- Relative or absolute path output
- Directory header support
- Clean terminal-friendly formatting
- Fast native C++ implementation
- Works well with AI/LLM workflows

## Build

Compile manually:

```bash
g++ -O2 -std=c++17 -o catall catall.cpp
```

## Usage

Basic usage:

```bash
./catall .
```

Export a source tree:

```bash
catall src/
```

Save output to file:

```bash
catall . > output.txt
```

## Header Customization

Custom header format:

```bash
catall -H '===== {path} =====' .
```

Relative path headers:

```bash
catall -H '--- FILE: {relpath} ---' .
```

Simple headers:

```bash
catall -S .
```

Enable directory headers:

```bash
catall -D .
```

## Available Header Tokens

- `{path}` — full file path
- `{relpath}` — relative file path

## Install from KocarTech Repository

### Ubuntu / Debian

```bash
curl -fsSL https://repo.kocar.net/repo.key | sudo gpg --dearmor -o /usr/share/keyrings/kocartech.gpg

echo "deb [signed-by=/usr/share/keyrings/kocartech.gpg] https://repo.kocar.net stable main" | sudo tee /etc/apt/sources.list.d/kocartech.list

sudo apt update
sudo apt install catall
```

## Repository

Official package repository:

https://repo.kocar.net/

Package description:

https://repo.kocar.net/description/catall/

## License

MIT License

Copyright (c) 2026 Gregor Kocar
