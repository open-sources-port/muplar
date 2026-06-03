#include "prefix.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/types.h>
#include <signal.h>

namespace muplar::runtime::prefix {
namespace {

struct InstanceRegistryEntry {
    std::string name;
    std::filesystem::path root;
};

bool looks_like_path(const std::string& spec)
{
    return spec.find('/') != std::string::npos ||
           spec.rfind("./", 0) == 0 ||
           spec.rfind("../", 0) == 0 ||
           spec.rfind("~/", 0) == 0;
}

std::filesystem::path home_dir()
{
    if (const char* home = std::getenv("HOME"); home && home[0])
        return home;
    throw std::runtime_error("HOME is not set; cannot resolve Muplar prefix");
}

std::filesystem::path expand_user_path(const std::string& spec)
{
    if (spec == "~")
        return home_dir();
    if (spec.rfind("~/", 0) == 0)
        return home_dir() / spec.substr(2);
    return spec;
}

std::filesystem::path absolute_normal_path(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::string quote_toml(const std::string& value)
{
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::optional<std::string> read_toml_string(const std::filesystem::path& path,
                                            const std::string& key)
{
    std::ifstream in(path);
    if (!in)
        return std::nullopt;

    std::string line;
    const std::string prefix = key + " = ";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) != 0)
            continue;
        std::string value = line.substr(prefix.size());
        if (value.size() < 2 || value.front() != '"' || value.back() != '"')
            return std::nullopt;
        value = value.substr(1, value.size() - 2);
        std::string decoded;
        decoded.reserve(value.size());
        bool escaped = false;
        for (char c : value) {
            if (escaped) {
                decoded.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else {
                decoded.push_back(c);
            }
        }
        return decoded;
    }
    return std::nullopt;
}

std::string quote_json_string(const std::string& value)
{
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                const char* hex = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

class JsonReader {
public:
    explicit JsonReader(std::string text)
        : text_(std::move(text))
    {
    }

    std::vector<InstanceRegistryEntry> parse_registry()
    {
        std::vector<InstanceRegistryEntry> entries;
        skip_ws();
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;

            std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            if (key == "instances") {
                entries = parse_instances();
            } else {
                skip_value();
            }

            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
        skip_ws();
        if (pos_ != text_.size())
            throw std::runtime_error("unexpected trailing JSON data");
        return entries;
    }

private:
    void skip_ws()
    {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected)
    {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != expected)
            return false;
        ++pos_;
        return true;
    }

    void expect(char expected)
    {
        if (!consume(expected)) {
            throw std::runtime_error(
                std::string("expected JSON token '") + expected + "'");
        }
    }

    std::string parse_string()
    {
        skip_ws();
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"')
                return out;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
                throw std::runtime_error("unterminated JSON escape");
            char esc = text_[pos_++];
            switch (esc) {
            case '"':
            case '\\':
            case '/':
                out.push_back(esc);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                // Registry paths are written as UTF-8 without \\u escapes.
                // Accept simple ASCII escapes so hand-edited files do not fail
                // on punctuation, but reject non-ASCII code points for now.
                if (pos_ + 4 > text_.size())
                    throw std::runtime_error("truncated JSON unicode escape");
                {
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = text_[pos_++];
                        value <<= 4;
                        if (h >= '0' && h <= '9')
                            value |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            value |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            value |= static_cast<unsigned>(h - 'A' + 10);
                        else
                            throw std::runtime_error("invalid JSON unicode escape");
                    }
                    if (value > 0x7F)
                        throw std::runtime_error("non-ASCII JSON unicode escape");
                    out.push_back(static_cast<char>(value));
                }
                break;
            default:
                throw std::runtime_error("invalid JSON escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    void skip_number()
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        while (pos_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
    }

    void skip_literal(const char* literal)
    {
        while (*literal) {
            if (pos_ >= text_.size() || text_[pos_++] != *literal++)
                throw std::runtime_error("invalid JSON literal");
        }
    }

    void skip_array()
    {
        expect('[');
        while (true) {
            skip_ws();
            if (consume(']'))
                break;
            skip_value();
            skip_ws();
            if (consume(','))
                continue;
            expect(']');
            break;
        }
    }

    void skip_object()
    {
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;
            (void)parse_string();
            skip_ws();
            expect(':');
            skip_value();
            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
    }

    void skip_value()
    {
        skip_ws();
        if (pos_ >= text_.size())
            throw std::runtime_error("unexpected end of JSON");
        char c = text_[pos_];
        if (c == '"') {
            (void)parse_string();
        } else if (c == '{') {
            skip_object();
        } else if (c == '[') {
            skip_array();
        } else if (c == 't') {
            skip_literal("true");
        } else if (c == 'f') {
            skip_literal("false");
        } else if (c == 'n') {
            skip_literal("null");
        } else if (c == '-' ||
                   std::isdigit(static_cast<unsigned char>(c))) {
            skip_number();
        } else {
            throw std::runtime_error("invalid JSON value");
        }
    }

    std::vector<InstanceRegistryEntry> parse_instances()
    {
        std::vector<InstanceRegistryEntry> entries;
        expect('[');
        while (true) {
            skip_ws();
            if (consume(']'))
                break;
            entries.push_back(parse_instance());
            skip_ws();
            if (consume(','))
                continue;
            expect(']');
            break;
        }
        return entries;
    }

    InstanceRegistryEntry parse_instance()
    {
        InstanceRegistryEntry entry;
        expect('{');
        while (true) {
            skip_ws();
            if (consume('}'))
                break;
            std::string key = parse_string();
            skip_ws();
            expect(':');
            if (key == "name") {
                entry.name = parse_string();
            } else if (key == "root") {
                entry.root = expand_user_path(parse_string());
            } else {
                skip_value();
            }
            skip_ws();
            if (consume(','))
                continue;
            expect('}');
            break;
        }
        return entry;
    }

    std::string text_;
    size_t pos_ = 0;
};

void ensure_layout_dirs(const PrefixLayout& layout)
{
    std::filesystem::create_directories(layout.rootfs / "tmp");
    std::filesystem::create_directories(layout.rootfs / "var" / "tmp");
    std::filesystem::create_directories(layout.packages_dir);
    std::filesystem::create_directories(layout.registry_dir);
    std::filesystem::create_directories(layout.apk_cache_dir);
    std::filesystem::create_directories(layout.logs_dir);

    switch (layout.kind) {
    case PrefixKind::Android:
        std::filesystem::create_directories(layout.rootfs / "data" / "app");
        std::filesystem::create_directories(layout.rootfs / "data" / "data");
        std::filesystem::create_directories(
            layout.rootfs / "data" / "local" / "tmp");
        std::filesystem::create_directories(layout.rootfs / "data" / "misc");
        std::filesystem::create_directories(layout.rootfs / "data" / "system");
        break;
    case PrefixKind::Linux:
        std::filesystem::create_directories(layout.rootfs / "etc");
        std::filesystem::create_directories(layout.rootfs / "home" / "muplar");
        std::filesystem::create_directories(layout.rootfs / "var");
        break;
    case PrefixKind::Wine:
        std::filesystem::create_directories(layout.rootfs / "drive_c");
        std::filesystem::create_directories(layout.rootfs / "home" / "muplar");
        std::filesystem::create_directories(layout.rootfs / "var");
        break;
    }
}

void assign_layout_paths(PrefixLayout& layout)
{
    layout.rootfs = layout.root / "rootfs";
    layout.packages_dir = layout.root / "packages";
    layout.registry_dir = layout.root / "registry";
    layout.cache_dir = layout.root / "cache";
    layout.apk_cache_dir = layout.cache_dir / "apk";
    layout.logs_dir = layout.root / "logs";
}

void write_prefix_toml(const PrefixLayout& layout, bool overwrite = false)
{
    std::filesystem::path path = layout.root / "prefix.toml";
    if (!overwrite && std::filesystem::exists(path))
        return;
    if (!is_supported_runtime_tuple(layout.kind, layout.arch)) {
        throw std::runtime_error(
            "unsupported runtime tuple: kind=" + to_string(layout.kind) +
            " arch=" + to_string(layout.arch));
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out)
        throw std::runtime_error("unable to write prefix metadata: " +
                                 path.string());

    out << "version = 1\n";
    out << "name = " << quote_toml(layout.name) << "\n";
    out << "kind = " << quote_toml(to_string(layout.kind)) << "\n";
    out << "arch = " << quote_toml(to_string(layout.arch)) << "\n";
    out << "runner = " << quote_toml(layout.runner) << "\n";
    if (!layout.runtime_sysroot.empty()) {
        out << "runtime_sysroot = "
            << quote_toml(std::filesystem::absolute(layout.runtime_sysroot).string())
            << "\n";
    }
}

std::string prefix_name_from_root(const std::filesystem::path& root)
{
    std::string name = root.filename().string();
    return name.empty() ? "default" : name;
}

bool path_is_same_or_inside(const std::filesystem::path& root,
                            const std::filesystem::path& path)
{
    auto root_norm = root.lexically_normal();
    auto path_norm = path.lexically_normal();
    auto root_it = root_norm.begin();
    auto path_it = path_norm.begin();
    for (; root_it != root_norm.end(); ++root_it, ++path_it) {
        if (path_it == path_norm.end() || *root_it != *path_it)
            return false;
    }
    return true;
}

bool same_path(const std::filesystem::path& a, const std::filesystem::path& b)
{
    return absolute_normal_path(a) == absolute_normal_path(b);
}

void write_instance_registry(const std::vector<InstanceRegistryEntry>& entries)
{
    std::filesystem::path path = instance_registry_path();
    std::filesystem::create_directories(path.parent_path());

    std::ofstream out(path, std::ios::trunc);
    if (!out)
        throw std::runtime_error("unable to write instance registry: " +
                                 path.string());

    out << "{\n";
    out << "  \"version\": 1,\n";
    out << "  \"instances\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out << "    {\n";
        out << "      \"name\": " << quote_json_string(entries[i].name) << ",\n";
        out << "      \"root\": "
            << quote_json_string(absolute_normal_path(entries[i].root).string())
            << "\n";
        out << "    }";
        if (i + 1 < entries.size())
            out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

std::vector<InstanceRegistryEntry> migrate_legacy_prefixes()
{
    std::vector<InstanceRegistryEntry> entries;
    std::filesystem::path base = muplar_home() / "prefixes";
    if (!std::filesystem::exists(base))
        return entries;

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_directory())
            continue;
        std::filesystem::path root = entry.path();
        if (!is_prefix_root(root))
            continue;

        std::string name = prefix_name_from_root(root);
        if (auto stored = read_toml_string(root / "prefix.toml", "name")) {
            if (!stored->empty())
                name = *stored;
        }
        entries.push_back({name, absolute_normal_path(root)});
    }
    return entries;
}

std::vector<InstanceRegistryEntry> read_instance_registry()
{
    std::filesystem::path path = instance_registry_path();
    if (!std::filesystem::exists(path)) {
        std::vector<InstanceRegistryEntry> migrated = migrate_legacy_prefixes();
        write_instance_registry(migrated);
        return migrated;
    }

    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("unable to read instance registry: " +
                                 path.string());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return JsonReader(buffer.str()).parse_registry();
}

std::optional<InstanceRegistryEntry> find_registered_instance(
    const std::string& name)
{
    for (const auto& entry : read_instance_registry()) {
        if (entry.name == name)
            return entry;
    }
    return std::nullopt;
}

void register_instance(const PrefixLayout& layout)
{
    if (layout.name.empty())
        return;

    std::vector<InstanceRegistryEntry> entries = read_instance_registry();
    std::vector<InstanceRegistryEntry> filtered;
    std::filesystem::path root = absolute_normal_path(layout.root);
    for (const auto& entry : entries) {
        if (entry.name == layout.name || same_path(entry.root, root))
            continue;
        filtered.push_back(entry);
    }
    filtered.push_back({layout.name, root});
    write_instance_registry(filtered);
}

void unregister_instance(const PrefixLayout& layout)
{
    std::vector<InstanceRegistryEntry> entries = read_instance_registry();
    std::vector<InstanceRegistryEntry> filtered;
    for (const auto& entry : entries) {
        if (entry.name == layout.name || same_path(entry.root, layout.root))
            continue;
        filtered.push_back(entry);
    }
    write_instance_registry(filtered);
}

PrefixLayout load_prefix_at_root(const std::filesystem::path& root,
                                 const std::string& instance_name,
                                 const std::filesystem::path& runtime_sysroot,
                                 bool create_if_missing,
                                 PrefixKind kind,
                                 GuestArch arch,
                                 std::string runner)
{
    PrefixLayout layout;
    layout.root = absolute_normal_path(root);
    layout.name = instance_name.empty() ? prefix_name_from_root(layout.root)
                                        : instance_name;
    if (layout.name.empty())
        layout.name = "default";

    layout.kind = kind;
    layout.arch = arch;
    layout.runner = runner.empty() ? "elfuse" : std::move(runner);
    assign_layout_paths(layout);
    layout.runtime_sysroot = runtime_sysroot;

    std::filesystem::path metadata = layout.root / "prefix.toml";
    bool metadata_exists = std::filesystem::is_regular_file(metadata);
    if (!metadata_exists && std::filesystem::exists(layout.root) &&
        !create_if_missing) {
        throw std::runtime_error("not a Muplar prefix: " +
                                 layout.root.string());
    }

    if (metadata_exists) {
        if (instance_name.empty()) {
            if (auto stored = read_toml_string(metadata, "name")) {
                if (!stored->empty())
                    layout.name = *stored;
            }
        }
        if (layout.runtime_sysroot.empty()) {
            if (auto stored = read_toml_string(metadata, "runtime_sysroot"))
                layout.runtime_sysroot = *stored;
        }
        if (auto stored = read_toml_string(metadata, "kind"))
            layout.kind = parse_prefix_kind(*stored);
        if (auto stored = read_toml_string(metadata, "arch"))
            layout.arch = parse_guest_arch(*stored);
        if (auto stored = read_toml_string(metadata, "runner"))
            layout.runner = *stored;
    }

    if (create_if_missing &&
        !is_supported_runtime_tuple(layout.kind, layout.arch)) {
        throw std::runtime_error(
            "unsupported runtime tuple: kind=" + to_string(layout.kind) +
            " arch=" + to_string(layout.arch));
    }

    if (!std::filesystem::exists(layout.root)) {
        if (!create_if_missing) {
            throw std::runtime_error(
                "prefix does not exist: " + layout.root.string());
        }
        ensure_layout_dirs(layout);
        write_prefix_toml(layout);
        register_instance(layout);
    } else {
        if (!std::filesystem::is_directory(layout.root)) {
            throw std::runtime_error("prefix path is not a directory: " +
                                     layout.root.string());
        }
        if (create_if_missing) {
            ensure_layout_dirs(layout);
            write_prefix_toml(layout);
            register_instance(layout);
        }
    }

    return layout;
}

PrefixLayout clone_prefix_into(const std::string& source_spec,
                               const std::string& dest_name,
                               const std::filesystem::path& dest_root,
                               bool replace_existing)
{
    if (dest_name.empty())
        throw std::runtime_error("destination prefix name is required");
    if (dest_root.empty())
        throw std::runtime_error("destination prefix root is required");

    PrefixLayout source = open_prefix(source_spec, {}, false);
    if (!is_prefix_root(source.root)) {
        throw std::runtime_error("source is not a Muplar prefix: " +
                                 source.root.string());
    }

    PrefixLayout dest = source;
    dest.root = absolute_normal_path(expand_user_path(dest_root.string()));
    dest.name = dest_name;
    assign_layout_paths(dest);

    std::filesystem::path source_abs = std::filesystem::weakly_canonical(
        source.root);
    std::filesystem::path dest_abs = std::filesystem::absolute(
        dest.root).lexically_normal();
    if (source_abs == dest_abs ||
        path_is_same_or_inside(source_abs, dest_abs) ||
        path_is_same_or_inside(dest_abs, source_abs)) {
        throw std::runtime_error("cannot clone overlapping prefix paths");
    }

    std::error_code ec;
    if (std::filesystem::exists(dest.root, ec)) {
        if (!replace_existing) {
            throw std::runtime_error(
                "destination prefix already exists: " + dest.root.string() +
                " (pass --replace to overwrite)");
        }
        std::filesystem::remove_all(dest.root, ec);
        if (ec)
            throw std::runtime_error("unable to remove destination prefix: " +
                                     ec.message());
    }

    std::filesystem::create_directories(dest.root.parent_path());
    std::filesystem::copy(
        source.root,
        dest.root,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::copy_symlinks);
    write_prefix_toml(dest, true);
    ensure_layout_dirs(dest);
    register_instance(dest);
    return dest;
}

} // namespace

std::filesystem::path muplar_home()
{
    if (const char* env = std::getenv("MUPLAR_HOME"); env && env[0])
        return expand_user_path(env);
    return home_dir() / ".muplar";
}

std::filesystem::path instance_registry_path()
{
    return muplar_home() / "instances.json";
}

std::filesystem::path resolve_prefix_root(const std::string& spec)
{
    if (spec.empty())
        return muplar_home() / "prefixes" / "default";
    if (looks_like_path(spec))
        return absolute_normal_path(expand_user_path(spec));
    if (auto registered_instance = find_registered_instance(spec))
        return absolute_normal_path(registered_instance->root);
    return muplar_home() / "prefixes" / spec;
}

std::string to_string(PrefixKind kind)
{
    switch (kind) {
    case PrefixKind::Android:
        return "android";
    case PrefixKind::Linux:
        return "linux";
    case PrefixKind::Wine:
        return "wine";
    }
    return "android";
}

std::string to_string(GuestArch arch)
{
    switch (arch) {
    case GuestArch::Aarch64:
        return "aarch64";
    case GuestArch::X86_64:
        return "x86_64";
    }
    return "aarch64";
}

PrefixKind parse_prefix_kind(const std::string& value)
{
    if (value.empty() || value == "android")
        return PrefixKind::Android;
    if (value == "linux")
        return PrefixKind::Linux;
    if (value == "wine" || value == "windows")
        return PrefixKind::Wine;
    throw std::runtime_error("unsupported prefix kind: " + value);
}

GuestArch parse_guest_arch(const std::string& value)
{
    if (value.empty() || value == "aarch64" || value == "arm64")
        return GuestArch::Aarch64;
    if (value == "x86_64" || value == "x64")
        return GuestArch::X86_64;
    throw std::runtime_error("unsupported prefix arch: " + value);
}

bool is_supported_runtime_tuple(PrefixKind kind, GuestArch arch)
{
    switch (kind) {
    case PrefixKind::Android:
        return arch == GuestArch::Aarch64;
    case PrefixKind::Wine:
        return arch == GuestArch::X86_64;
    case PrefixKind::Linux:
        return arch == GuestArch::Aarch64 || arch == GuestArch::X86_64;
    }
    return false;
}

PrefixLayout open_prefix(const std::string& spec,
                         const std::filesystem::path& runtime_sysroot,
                         bool create_if_missing,
                         PrefixKind kind,
                         GuestArch arch,
                         std::string runner)
{
    std::filesystem::path root = resolve_prefix_root(spec);
    std::string name = looks_like_path(spec) ? std::string() : spec;
    return load_prefix_at_root(root, name, runtime_sysroot, create_if_missing,
                               kind, arch, std::move(runner));
}

PrefixLayout open_prefix_at_root(const std::string& name,
                                 const std::filesystem::path& root,
                                 const std::filesystem::path& runtime_sysroot,
                                 bool create_if_missing,
                                 PrefixKind kind,
                                 GuestArch arch,
                                 std::string runner)
{
    if (name.empty())
        throw std::runtime_error("prefix instance name is required");
    return load_prefix_at_root(expand_user_path(root.string()), name,
                               runtime_sysroot, create_if_missing, kind, arch,
                               std::move(runner));
}

std::vector<PrefixLayout> list_prefixes()
{
    std::vector<PrefixLayout> out;
    std::vector<InstanceRegistryEntry> entries = read_instance_registry();

    for (const auto& entry : entries) {
        try {
            PrefixLayout layout = load_prefix_at_root(
                entry.root, entry.name, {}, false, PrefixKind::Android,
                GuestArch::Aarch64, "elfuse");
            out.push_back(layout);
        } catch (const std::exception&) {
            // Keep the registry entry. The root may live on removable storage
            // or another user-selected location that is temporarily absent.
        }
    }

    return out;
}

bool is_prefix_root(const std::filesystem::path& root)
{
    std::error_code ec;
    return std::filesystem::is_directory(root, ec) &&
           std::filesystem::is_regular_file(root / "prefix.toml", ec);
}

PrefixLayout clone_prefix(const std::string& source_spec,
                          const std::string& dest_spec,
                          bool replace_existing)
{
    std::filesystem::path dest_root = resolve_prefix_root(dest_spec);
    std::string dest_name = looks_like_path(dest_spec)
        ? prefix_name_from_root(dest_root)
        : dest_spec;
    if (dest_name.empty())
        dest_name = "default";
    return clone_prefix_into(source_spec, dest_name, dest_root,
                             replace_existing);
}

PrefixLayout clone_prefix_to_root(const std::string& source_spec,
                                  const std::string& dest_name,
                                  const std::filesystem::path& dest_root,
                                  bool replace_existing)
{
    return clone_prefix_into(source_spec, dest_name, dest_root,
                             replace_existing);
}

void delete_prefix(const std::string& spec)
{
    PrefixLayout layout = open_prefix(spec, {}, false);
    if (!is_prefix_root(layout.root)) {
        throw std::runtime_error("not a Muplar prefix: " +
                                 layout.root.string());
    }

    std::error_code ec;
    std::filesystem::remove_all(layout.root, ec);
    if (ec) {
        throw std::runtime_error("unable to delete prefix " +
                                 layout.root.string() + ": " +
                                 ec.message());
    }
    unregister_instance(layout);
}

// ---------------------------------------------------------------------------
// Instance lifecycle helpers (PID file based)
// ---------------------------------------------------------------------------

std::filesystem::path pid_file_path(const PrefixLayout& layout)
{
    return layout.root / "run" / "wine.pid";
}

pid_t read_prefix_pid(const PrefixLayout& layout)
{
    std::filesystem::path pid_path = pid_file_path(layout);
    std::ifstream in(pid_path);
    if (!in)
        return 0;
    pid_t pid = 0;
    in >> pid;
    return (in && pid > 0) ? pid : 0;
}

PrefixState query_prefix_state(const PrefixLayout& layout)
{
    pid_t pid = read_prefix_pid(layout);
    if (pid <= 0)
        return PrefixState::Stopped;

    // kill(pid, 0) succeeds if the process exists (even if we can't signal it).
    if (kill(pid, 0) == 0)
        return PrefixState::Running;

    // Stale PID file — process is gone, clean it up.
    std::error_code ec;
    std::filesystem::remove(pid_file_path(layout), ec);
    return PrefixState::Stopped;
}

} // namespace muplar::runtime::prefix
