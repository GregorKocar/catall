#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct Options {
    fs::path dir = ".";

    bool recursive = false;
    bool absolute = false;
    bool follow_symlinks = false;
    bool dir_headers = false;
    bool simple_headers = false;
    bool quiet = false;

    bool text_only = true;
    bool max_size_enabled = true;
	
	bool show_skipped = false;
    bool show_skipped_reasons = false;
	
	bool include_hidden = false;
    bool use_gitignore = true;
	
	bool limit_text_enabled = false;
	std::uintmax_t limit_text = 0;

	bool limit_files_enabled = false;
	std::uintmax_t limit_files = 0;
	std::uintmax_t files_printed = 0;

    std::string header_fmt = "===== {path} =====";
    std::string dir_header_fmt = "=== DIRECTORY: {path} ===";
    std::string separator = "\n\n";

    std::uintmax_t max_size = 1024 * 1024;

    std::vector<std::string> include_exts;
    std::vector<std::string> exclude_exts;
    std::vector<std::string> extcols;
    std::vector<std::string> excludes;
};

static std::string trim(std::string s) {
    auto ns = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string normalize_ext(std::string e) {
    e = trim(lower(e));
    while (!e.empty() && e.front() == '.') e.erase(e.begin());
    return e;
}

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;

    while (std::getline(ss, item, ',')) {
        item = normalize_ext(item);
        if (!item.empty()) out.push_back(item);
    }

    return out;
}

static std::string unescape(std::string s) {
    std::string out;

    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];

            if (c == 'n') out += '\n';
            else if (c == 't') out += '\t';
            else if (c == 'r') out += '\r';
            else out += c;
        } else {
            out += s[i];
        }
    }

    return out;
}

static void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;

    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::map<std::string, std::vector<std::string>> builtin_collections() {
    return {
        {"cpp", {
            "c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx",
            "ipp", "tpp", "ixx", "cppm", "cmake"
        }},
        {"python", {
            "py", "pyi", "pyx", "pxd", "toml", "cfg", "ini",
            "txt", "md", "yml", "yaml"
        }},
        {"php", {
            "php", "phtml", "php5", "php8", "blade.php", "inc",
            "json", "xml", "env", "md"
        }},
        {"nodejs", {
            "js", "mjs", "cjs", "ts", "tsx", "jsx", "json", "jsonc",
            "vue", "svelte", "css", "scss", "sass", "less", "html",
            "md", "yml", "yaml", "env"
        }},
        {"web", {
            "html", "htm", "css", "scss", "sass", "less", "js",
            "mjs", "cjs", "ts", "tsx", "jsx", "vue", "svelte",
            "json", "svg", "md"
        }},
        {"java", {
            "java", "kt", "kts", "gradle", "xml", "properties",
            "yml", "yaml", "json", "md"
        }},
        {"go", {
            "go", "mod", "sum", "tmpl", "yaml", "yml", "json", "md"
        }},
        {"rust", {
            "rs", "toml", "lock", "md"
        }},
        {"dotnet", {
            "cs", "csproj", "sln", "props", "targets", "json",
            "xml", "config", "md"
        }},
        {"ruby", {
            "rb", "erb", "gemspec", "rake", "yml", "yaml", "json", "md"
        }},
        {"shell", {
            "sh", "bash", "zsh", "fish", "env", "profile"
        }},
        {"config", {
            "json", "jsonc", "yaml", "yml", "toml", "ini", "cfg",
            "conf", "env", "properties", "xml"
        }}
    };
}

static std::set<std::string> builtin_binary_exts() {
    return {
        "png", "jpg", "jpeg", "gif", "webp", "ico", "bmp", "tiff",
        "zip", "gz", "xz", "bz2", "7z", "rar", "tar", "tgz",
        "pdf",
        "o", "so", "a", "dll", "exe", "class", "jar", "war",
        "mp3", "mp4", "mkv", "mov", "avi", "flac", "wav",
        "sqlite", "db",
        "woff", "woff2", "ttf", "otf",
        "pyc", "pyo"
    };
}

static void load_config(
    std::map<std::string, std::vector<std::string>>& collections,
    std::set<std::string>& binary_exts,
    const fs::path& path = "/etc/catall/catall.conf"
) {
    std::ifstream in(path);
    if (!in) return;

    std::string line;
    std::string section;

    while (std::getline(in, line)) {
        line = trim(line);

        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string name = lower(trim(line.substr(0, eq)));
        std::string value = line.substr(eq + 1);

        if (section == "collections") {
            if (!name.empty()) {
                collections[name] = split_csv(value);
            }
        } else if (section == "binary_exts") {
            for (const auto& e : split_csv(value)) {
                binary_exts.insert(e);
            }
        }
    }
}

static void usage() {
    std::cout <<
R"(catall - cat files with path headers

Usage:
  catall [options] DIR

Without arguments, catall shows this help.

Normal examples:
  catall .
  catall -r .
  catall -A .
  catall -r -A .
  catall -r -D .
  catall -r -D -A .
  catall -S .
  catall -S -A .
  catall -r -S -A .
  catall -H '--- FILE: {relpath} ---' .
  catall -A -H '===== {path} =====' .
  catall -r -s '\n--- END FILE ---\n' .

Project examples:
  catall --extcol cpp -r .
  catall --extcol python -r .
  catall --extcol nodejs --exclude-ext json -r .
  catall --extcol php -e vue -e ts -x vendor -x .git -r .
  catall --extcol laravel -r .
  catall --list-extcols

Options:
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

  -b, --binary         Include binary-looking files and known binary extensions
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
  -T, --limit-text CHARS       Limit displayed file content to CHARS characters, then print ...
  -F, --limit-files COUNT      Limit number of displayed files, then print ==...MORE FILES...==
  -q, --quiet                  Suppress read/permission/filesystem errors

      --show-skipped           Show skipped files
      --show-skipped-reasons   Show skipped files with reasons

      --list-extcols           Show available extension collections
  -h, --help                   Show help

Built-in extension collections:
  cpp, python, php, nodejs, web, java, go, rust, dotnet, ruby, shell, config

Config file:
  /etc/catall/catall.conf

Config example:
  [collections]
  laravel=php,blade.php,js,ts,vue,css,scss,json,env,md
  django=py,html,css,js,json,yml,yaml,toml,md

  [binary_exts]
  extra=wasm,map,bin,dat
)";
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static bool simple_excluded(const std::string& rel, const std::vector<std::string>& patterns) {
    for (auto pat : patterns) {
        pat = trim(pat);

        while (!pat.empty() && pat.front() == '/') {
            pat.erase(pat.begin());
        }

        if (pat.empty()) continue;

        if (pat.back() == '*') {
            pat.pop_back();
            if (starts_with(rel, pat)) return true;
        } else {
            if (rel == pat || starts_with(rel, pat + "/")) return true;
        }
    }

    return false;
}

static bool looks_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;

    constexpr size_t sample_size = 8192;

    char buf[sample_size];
    in.read(buf, sample_size);

    std::streamsize n = in.gcount();

    if (n <= 0) return true;

    size_t suspicious = 0;

    for (std::streamsize i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(buf[i]);

        if (c == 0) return false;

        if (c < 7 || (c > 13 && c < 32)) {
            suspicious++;
        }
    }

    double ratio = static_cast<double>(suspicious) / static_cast<double>(n);
    return ratio < 0.05;
}

static Options parse_args(
    int argc,
    char** argv,
    const std::map<std::string, std::vector<std::string>>& collections
) {
    Options opt;

    if (argc == 1) {
        usage();
        std::exit(0);
    }

    auto need_arg = [&](int& i, const std::string& name) -> std::string {
        if (i + 1 >= argc) {
            std::cerr << name << " requires an argument\n";
            std::exit(2);
        }

        return argv[++i];
    };

    bool dir_set = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
		if (a.size() > 2 && a[0] == '-' && a[1] != '-') {
			for (size_t j = 1; j < a.size(); ++j) {
				char c = a[j];

				switch (c) {
					case 'r': opt.recursive = true; break;
					case 'A': opt.absolute = true; break;
					case 'S': opt.simple_headers = true; break;
					case 'D': opt.dir_headers = true; break;
					case 'L': opt.follow_symlinks = true; break;
					case 'q': opt.quiet = true; break;
					case 'B': opt.text_only = false; break;
					case 'M': opt.max_size_enabled = false; break;

					case 'h':
						usage();
						std::exit(0);

					case 'T':
					case 'F':
					case 'm':
					case 'H':
					case 's':
					case 'e':
					case 'x':
						std::cerr
							<< "Option -" << c
							<< " requires an argument and cannot be combined. "
							<< "Use -" << c << " VALUE instead.\n";
						std::exit(2);

					default:
						std::cerr << "Unknown short option: -" << c << "\n";
						std::exit(2);
				}
			}

			continue;
		}

        if (a == "-r" || a == "--recursive") opt.recursive = true;
        else if (a == "-A" || a == "--absolute") opt.absolute = true;
        else if (a == "-S" || a == "--simple") opt.simple_headers = true;
        else if (a == "-D" || a == "--dir-headers") opt.dir_headers = true;
        else if (a == "-L" || a == "--follow-symlinks") opt.follow_symlinks = true;
        else if (a == "-q" || a == "--quiet") opt.quiet = true;
        else if (a == "-b" || a == "--binary") opt.text_only = false;
        else if (a == "-M" || a == "--no-max-size") opt.max_size_enabled = false;
        else if (a == "-h" || a == "--help") {
            usage();
            std::exit(0);
        }
        else if (a == "--list-extcols") {
            for (const auto& [name, exts] : collections) {
                std::cout << name << ": ";

                for (size_t j = 0; j < exts.size(); ++j) {
                    if (j) std::cout << ", ";
                    std::cout << exts[j];
                }

                std::cout << "\n";
            }

            std::exit(0);
        }
        else if (a == "-H" || a == "--header") {
            opt.header_fmt = unescape(need_arg(i, a));
        }
        else if (a == "--dir-header") {
            opt.dir_header_fmt = unescape(need_arg(i, a));
        }
        else if (a == "-s" || a == "--separator") {
            opt.separator = unescape(need_arg(i, a));
        }
        else if (a == "-e" || a == "--ext") {
            opt.include_exts.push_back(normalize_ext(need_arg(i, a)));
        }
        else if (a == "--extcol") {
            opt.extcols.push_back(lower(trim(need_arg(i, a))));
        }
        else if (a == "--exclude-ext") {
            opt.exclude_exts.push_back(normalize_ext(need_arg(i, a)));
        }
        else if (a == "-x" || a == "--exclude") {
            opt.excludes.push_back(need_arg(i, a));
        }
		else if (a == "--show-skipped") {
			opt.show_skipped = true;
		}
		else if (a == "--show-skipped-reasons") {
			opt.show_skipped = true;
			opt.show_skipped_reasons = true;
		}
		else if (a == "--include-hidden") {
			opt.include_hidden = true;
		}
		else if (a == "--no-gitignore") {
			opt.use_gitignore = false;
		}
		else if (a == "-T" || a == "--limit-text") {
			std::string v = need_arg(i, a);

			if (v.empty() || !std::all_of(v.begin(), v.end(), [](unsigned char c) {
				return std::isdigit(c);
			})) {
				std::cerr << a << " must be a number of characters, e.g. 4000\n";
				std::exit(2);
			}

			opt.limit_text = std::stoull(v);
			opt.limit_text_enabled = true;
		}
		else if (a == "-F" || a == "--limit-files") {
			std::string v = need_arg(i, a);

			if (v.empty() || !std::all_of(v.begin(), v.end(), [](unsigned char c) {
				return std::isdigit(c);
			})) {
				std::cerr << a << " must be a number of files, e.g. 20\n";
				std::exit(2);
			}

			opt.limit_files = std::stoull(v);
			opt.limit_files_enabled = true;
		}
        else if (a == "-m" || a == "--max-size") {
            std::string v = need_arg(i, a);

            if (v.empty() || !std::all_of(v.begin(), v.end(), [](unsigned char c) {
                return std::isdigit(c);
            })) {
                std::cerr << "--max-size must be bytes, e.g. 1048576\n";
                std::exit(2);
            }

            opt.max_size = std::stoull(v);
            opt.max_size_enabled = true;
        }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            std::exit(2);
        }
        else {
            if (dir_set) {
                std::cerr << "Unexpected extra argument: " << a << "\n";
                std::exit(2);
            }

            opt.dir = a;
            dir_set = true;
        }
    }

    if (!dir_set) {
        usage();
        std::exit(0);
    }

    return opt;
}

static std::set<std::string> build_extension_set(
    const Options& opt,
    const std::map<std::string, std::vector<std::string>>& collections
) {
    std::set<std::string> result;

    for (const auto& col : opt.extcols) {
        auto it = collections.find(col);

        if (it == collections.end()) {
            std::cerr << "Unknown extension collection: " << col << "\n";
            std::cerr << "Use --list-extcols to see available collections.\n";
            std::exit(2);
        }

        for (const auto& e : it->second) {
            if (!e.empty()) {
                result.insert(normalize_ext(e));
            }
        }
    }

    for (const auto& e : opt.include_exts) {
        if (!e.empty()) {
            result.insert(normalize_ext(e));
        }
    }

    for (const auto& e : opt.exclude_exts) {
        result.erase(normalize_ext(e));
    }

    return result;
}

static std::vector<std::string> compound_exts() {
    return {
        "blade.php"
    };
}

static std::string file_extension_key(const fs::path& p) {
    std::string filename = lower(p.filename().string());

    for (const auto& ext : compound_exts()) {
        std::string suffix = "." + ext;

        if (filename.size() >= suffix.size() &&
            filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return ext;
        }
    }

    return normalize_ext(p.extension().string());
}

static fs::path display_path(
    const fs::path& path,
    const fs::path& root,
    bool absolute
) {
    std::error_code ec;

    if (absolute) {
        fs::path abs = fs::absolute(path, ec);
        return ec ? path : abs;
    }

    fs::path rel = fs::relative(path, root, ec);
    return ec ? path.filename() : rel;
}

static std::string rel_string(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path rel = fs::relative(path, root, ec);
    return ec ? path.filename().generic_string() : rel.generic_string();
}

static void print_header_line(
    const Options& opt,
    const fs::path& path,
    const fs::path& root,
    bool is_dir
) {
    fs::path shown_path = display_path(path, root, opt.absolute);
    std::string shown = shown_path.string();
    std::string rel = rel_string(path, root);

    if (opt.simple_headers && !is_dir) {
        std::cout << shown << ":\n";
        return;
    }

    std::string header = is_dir ? opt.dir_header_fmt : opt.header_fmt;

    replace_all(header, "{path}", shown);
    replace_all(header, "{relpath}", rel);

    std::cout << header << "\n";
}
static void print_skipped(
    const Options& opt,
    const fs::path& path,
    const fs::path& root,
    const std::string& reason
) {
    if (!opt.show_skipped)
        return;

    fs::path shown = display_path(path, root, opt.absolute);

    if (opt.show_skipped_reasons) {
        std::cout
            << "=== SKIPPED: "
            << shown.string()
            << " ("
            << reason
            << ") ===\n";
    } else {
        std::cout
            << "=== SKIPPED: "
            << shown.string()
            << " ===\n";
    }

    std::cout << opt.separator;
}
static bool copy_file_to_stdout_limited(
    std::ifstream& in,
    const Options& opt
) {
    constexpr std::size_t buf_size = 8192;
    char buf[buf_size];

    if (!opt.limit_text_enabled) {
        std::cout << in.rdbuf();
        return false;
    }

    std::uintmax_t remaining = opt.limit_text;
    bool truncated = false;

    while (remaining > 0 && in) {
        std::size_t want = static_cast<std::size_t>(
            std::min<std::uintmax_t>(remaining, buf_size)
        );

        in.read(buf, static_cast<std::streamsize>(want));
        std::streamsize got = in.gcount();

        if (got <= 0) break;

        std::cout.write(buf, got);
        remaining -= static_cast<std::uintmax_t>(got);
    }

    if (in.peek() != EOF) {
        truncated = true;
        std::cout << "...";
    }

    return truncated;
}

static bool is_hidden_path(const fs::path& path, const fs::path& root) {
    std::string rel = rel_string(path, root);

    std::stringstream ss(rel);
    std::string part;

    while (std::getline(ss, part, '/')) {
        if (!part.empty() && part[0] == '.') {
            return true;
        }
    }

    return false;
}

static std::vector<std::string> load_gitignore_patterns(const fs::path& root) {
    std::vector<std::string> patterns;

    std::ifstream in(root / ".gitignore");
    if (!in) return patterns;

    std::string line;

    while (std::getline(in, line)) {
        line = trim(line);

        if (line.empty()) continue;
        if (line[0] == '#') continue;
        if (line[0] == '!') continue; // negation not supported in this simple version

        while (!line.empty() && line.front() == '/') {
            line.erase(line.begin());
        }

        if (!line.empty()) {
            patterns.push_back(line);
        }
    }

    return patterns;
}

static bool gitignore_excluded(const std::string& rel, const std::vector<std::string>& patterns) {
    for (auto pat : patterns) {
        pat = trim(pat);
        if (pat.empty()) continue;

        bool dir_only = false;

        if (!pat.empty() && pat.back() == '/') {
            dir_only = true;
            pat.pop_back();
        }

        if (pat.empty()) continue;

        if (pat.front() == '*') {
            pat.erase(pat.begin());
            if (rel.size() >= pat.size() &&
                rel.compare(rel.size() - pat.size(), pat.size(), pat) == 0) {
                return true;
            }
        }

        if (pat.back() == '*') {
            pat.pop_back();
            if (starts_with(rel, pat)) return true;
        }

        if (rel == pat || starts_with(rel, pat + "/")) {
            return true;
        }

        if (rel.size() > pat.size() &&
            rel.compare(rel.size() - pat.size(), pat.size(), pat) == 0) {
            size_t pos = rel.size() - pat.size();
            if (pos == 0 || rel[pos - 1] == '/') {
                return true;
            }
        }

        (void)dir_only;
    }

    return false;
}

int main(int argc, char** argv) {
    auto collections = builtin_collections();
    auto binary_exts = builtin_binary_exts();

    load_config(collections, binary_exts);

    Options opt = parse_args(argc, argv, collections);

    std::set<std::string> allowed_exts = build_extension_set(opt, collections);

    for (const auto& e : opt.include_exts) {
        binary_exts.erase(normalize_ext(e));
    }

    if (!fs::is_directory(opt.dir)) {
        std::cerr << "Not a directory: " << opt.dir << "\n";
        return 2;
    }

    fs::path root;

	try {
		root = fs::canonical(opt.dir);
	} catch (const fs::filesystem_error& e) {
		if (!opt.quiet) {
			std::cerr << "Could not resolve directory: " << opt.dir << ": " << e.what() << "\n";
		}

		return 1;
	}

	std::vector<std::string> gitignore_patterns;

	if (opt.use_gitignore) {
		gitignore_patterns = load_gitignore_patterns(root);
	}

	fs::directory_options dopt = fs::directory_options::skip_permission_denied;
    if (opt.follow_symlinks) {
        dopt |= fs::directory_options::follow_directory_symlink;
    }

    std::vector<fs::path> entries;

	try {
		if (opt.recursive) {
			fs::recursive_directory_iterator it(root, dopt);
			fs::recursive_directory_iterator end;

			for (; it != end; ++it) {
				fs::path p = it->path();
				std::string rel = rel_string(p, root);

				if (!opt.include_hidden && is_hidden_path(p, root)) {
					if (it->is_directory()) {
						it.disable_recursion_pending();
					}
					entries.push_back(p);
					continue;
				}

				if (opt.use_gitignore && gitignore_excluded(rel, gitignore_patterns)) {
					if (it->is_directory()) {
						it.disable_recursion_pending();
					}
					entries.push_back(p);
					continue;
				}

				entries.push_back(p);
			}
		} else {
			for (const auto& entry : fs::directory_iterator(root, dopt)) {
				entries.push_back(entry.path());
			}
		}
	} catch (const fs::filesystem_error& e) {
	if (!opt.quiet) {
            std::cerr << "Filesystem traversal error: " << e.what() << "\n";
        }

        return 1;
    }

    std::sort(entries.begin(), entries.end());

    for (const auto& p : entries) {
        try {
            std::error_code ec;

            std::string rel = rel_string(p, root);
			if (!opt.include_hidden && is_hidden_path(p, root)) {
				print_skipped(opt, p, root, "hidden path");
				continue;
			}

			if (opt.use_gitignore && gitignore_excluded(rel, gitignore_patterns)) {
				print_skipped(opt, p, root, "gitignore");
				continue;
			}
			
			if (simple_excluded(rel, opt.excludes)) {
				print_skipped(opt, p, root, "excluded path");
				continue;
			}

            if (fs::is_directory(p, ec)) {
                if (opt.dir_headers) {
                    print_header_line(opt, p, root, true);
                    std::cout << opt.separator;
                }

                continue;
            }

            if (!fs::is_regular_file(p, ec)) {
                continue;
            }

            std::string ext = file_extension_key(p);

            if (!allowed_exts.empty()) {
				if (!allowed_exts.count(ext)) {
					print_skipped(opt, p, root, "extension filter");
					continue;
				}
            }

            if (opt.max_size_enabled) {
                auto size = fs::file_size(p, ec);

				if (ec || size > opt.max_size) {
					print_skipped(opt, p, root, "size limit");
					continue;
				}
            }

            if (opt.text_only) {
				if (binary_exts.count(ext)) {
					print_skipped(opt, p, root, "binary extension");
					continue;
				}

				if (!looks_text(p)) {
					print_skipped(opt, p, root, "binary detected");
					continue;
				}
            }
			if (opt.limit_files_enabled &&
				opt.files_printed >= opt.limit_files) {
				std::cout << "==...MORE FILES...==\n";
				break;
			}
            print_header_line(opt, p, root, false);

            errno = 0;

            std::ifstream in(p, std::ios::binary);

            if (!in) {
                if (!opt.quiet) {
                    std::cerr
                        << "Could not read: "
                        << p
                        << ": "
                        << std::strerror(errno ? errno : EACCES)
                        << "\n";
                }

                continue;
            }

			copy_file_to_stdout_limited(in, opt);

			std::cout << opt.separator;

			opt.files_printed++;
        }
        catch (const fs::filesystem_error& e) {
            if (!opt.quiet) {
                std::cerr << "Filesystem error: " << p << ": " << e.what() << "\n";
            }
        }
        catch (const std::exception& e) {
            if (!opt.quiet) {
                std::cerr << "Error processing: " << p << ": " << e.what() << "\n";
            }
        }
    }

    return 0;
}