# catall

catall is a recursive code and text export utility written in C++ for Linux.

It recursively scans directories and concatenates source and text files into a single readable output stream. It is designed for:

-   AI context export
    
-   source code reviews
    
-   backups
    
-   debugging
    
-   code analysis
    
-   documentation generation
    
-   terminal-based code inspection
    

## Features

-   Recursive directory scanning
    
-   Configurable file headers
    
-   Relative or absolute path output
    
-   Directory header support
    
-   Extension filtering
    
-   Hidden file handling
    
-   Binary detection
    
-   File size limits
    
-   Symlink support
    
-   Gitignore support
    
-   Fast native C++ implementation
    
-   Works well with AI/LLM workflows
    

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

Export recursively:

```bash
catall -r .

```

Save output to file:

```bash
catall -r . > output.txt

```

## Install from KocarTech Repository

### Ubuntu / Debian

```bash
curl -fsSL https://repo.kocar.net/repo.key | sudo gpg --dearmor -o /usr/share/keyrings/kocartech.gpg

echo "deb [signed-by=/usr/share/keyrings/kocartech.gpg] https://repo.kocar.net stable main" | sudo tee /etc/apt/sources.list.d/kocartech.list

sudo apt update
sudo apt install catall

```

## Examples

Custom headers:

```bash
catall -H '===== {path} =====' .

```

Relative path headers:

```bash
catall -H '--- FILE: {relpath} ---' .

```

Directory headers:

```bash
catall -D .

```

Limit output:

```bash
catall -T 1000 -F 50 .

```

Include hidden files:

```bash
catall --include-hidden .

```

## Options

```text
  -r, --recursive              Recurse into subdirectories
  -A, --absolute               Print absolute paths in headers
  -S, --simple                 Simple file headers: path:
  -D, --dir-headers            Print directory headers
      --dir-header FMT         Directory header format
                               Default: "=== DIRECTORY: {path} ==="

  -H, --header FMT             File header format
                               Default: "===== {path} ====="
                               Tokens: {path} {relpath}

  -s, --separator STR          Printed after each file. Default: "\n\n"

  -b, --binary                 Include binary-looking files and known binary extensions
                               Default skips binary extensions and binary-looking files

  -m, --max-size BYTES         Skip files larger than BYTES
                               Default: 1048576 bytes

  -M, --no-max-size            Disable default max-size limit

  -e, --ext EXT                Add extension manually, repeatable
                               Also overrides binary-extension exclusion for EXT

      --extcol NAME            Add extension collection, repeatable
      --exclude-ext EXT        Remove extension from final extension set

  -x, --exclude PATH_OR_PREFIX Exclude relative path/prefix, repeatable

      --include-hidden         Include dot files and dot directories
      --no-gitignore           Do not read .gitignore

  -L, --follow-symlinks        Follow symlinks

  -T, --limit-text CHARS       Limit displayed file content to CHARS
                               then print ...

  -F, --limit-files COUNT      Limit displayed files
                               then print ==...MORE FILES...==

  -q, --quiet                  Suppress filesystem/read errors

      --show-skipped           Show skipped files
      --show-skipped-reasons   Show skipped files with reasons

      --list-extcols           Show available extension collections

  -h, --help                   Show help

```

## Repository

Official package repository:

[https://repo.kocar.net/](https://repo.kocar.net/)

Package description:

[https://repo.kocar.net/description/catall/](https://repo.kocar.net/description/catall/)

## License

MIT License

Copyright (c) 2026 Gregor Kocar
