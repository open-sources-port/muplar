#include "apk_envelope.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <zlib.h>

namespace muplar::runtime::apk {
namespace {

struct ZipEntry {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t compressed_size = 0;
    uint32_t uncompressed_size = 0;
    uint32_t local_header_offset = 0;
};

uint16_t read_u16(const std::vector<uint8_t>& data, size_t off)
{
    if (off + 2 > data.size())
        throw std::runtime_error("APK zip header truncated");
    return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
}

uint32_t read_u32(const std::vector<uint8_t>& data, size_t off)
{
    if (off + 4 > data.size())
        throw std::runtime_error("APK zip header truncated");
    return static_cast<uint32_t>(data[off]) |
           (static_cast<uint32_t>(data[off + 1]) << 8) |
           (static_cast<uint32_t>(data[off + 2]) << 16) |
           (static_cast<uint32_t>(data[off + 3]) << 24);
}

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("unable to open APK: " + path.string());

    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0)
        throw std::runtime_error("unable to size APK: " + path.string());
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty())
        in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in && size > 0)
        throw std::runtime_error("unable to read APK: " + path.string());
    return data;
}

std::vector<ZipEntry> read_zip_entries(const std::vector<uint8_t>& apk)
{
    constexpr uint32_t EOCD_SIG = 0x06054b50;
    constexpr uint32_t CEN_SIG = 0x02014b50;
    constexpr size_t EOCD_MIN_SIZE = 22;
    constexpr size_t EOCD_MAX_COMMENT = 65535;

    if (apk.size() < EOCD_MIN_SIZE)
        throw std::runtime_error("APK is too small to be a zip file");

    size_t search_start = apk.size() > EOCD_MIN_SIZE + EOCD_MAX_COMMENT
        ? apk.size() - (EOCD_MIN_SIZE + EOCD_MAX_COMMENT)
        : 0;
    size_t eocd = std::string::npos;
    for (size_t off = apk.size() - EOCD_MIN_SIZE + 1; off-- > search_start;) {
        if (read_u32(apk, off) == EOCD_SIG) {
            eocd = off;
            break;
        }
    }
    if (eocd == std::string::npos)
        throw std::runtime_error("APK zip end-of-central-directory not found");

    uint16_t entry_count = read_u16(apk, eocd + 10);
    uint32_t central_size = read_u32(apk, eocd + 12);
    uint32_t central_off = read_u32(apk, eocd + 16);
    if (central_off == 0xffffffffu || central_size == 0xffffffffu)
        throw std::runtime_error("ZIP64 APKs are not supported yet");
    if (static_cast<uint64_t>(central_off) + central_size > apk.size())
        throw std::runtime_error("APK central directory is out of bounds");

    std::vector<ZipEntry> entries;
    size_t off = central_off;
    for (uint16_t i = 0; i < entry_count; ++i) {
        if (off + 46 > apk.size() || read_u32(apk, off) != CEN_SIG)
            throw std::runtime_error("APK central directory entry is invalid");

        uint16_t name_len = read_u16(apk, off + 28);
        uint16_t extra_len = read_u16(apk, off + 30);
        uint16_t comment_len = read_u16(apk, off + 32);
        if (off + 46u + name_len + extra_len + comment_len > apk.size())
            throw std::runtime_error("APK central directory entry is truncated");

        ZipEntry entry;
        entry.flags = read_u16(apk, off + 8);
        entry.method = read_u16(apk, off + 10);
        entry.compressed_size = read_u32(apk, off + 20);
        entry.uncompressed_size = read_u32(apk, off + 24);
        entry.local_header_offset = read_u32(apk, off + 42);
        entry.name.assign(reinterpret_cast<const char*>(apk.data() + off + 46),
                          name_len);
        entries.push_back(entry);

        off += 46u + name_len + extra_len + comment_len;
    }
    return entries;
}

std::vector<uint8_t> extract_entry_data(const std::vector<uint8_t>& apk,
                                        const ZipEntry& entry)
{
    constexpr uint32_t LOC_SIG = 0x04034b50;
    size_t off = entry.local_header_offset;
    if (off + 30 > apk.size() || read_u32(apk, off) != LOC_SIG)
        throw std::runtime_error("APK local zip header is invalid for " + entry.name);

    uint16_t name_len = read_u16(apk, off + 26);
    uint16_t extra_len = read_u16(apk, off + 28);
    size_t data_off = off + 30u + name_len + extra_len;
    if (data_off + entry.compressed_size > apk.size())
        throw std::runtime_error("APK entry data is truncated for " + entry.name);

    const uint8_t* src = apk.data() + data_off;
    if (entry.method == 0) {
        return std::vector<uint8_t>(src, src + entry.compressed_size);
    }
    if (entry.method != 8) {
        throw std::runtime_error("unsupported APK zip compression method " +
                                 std::to_string(entry.method) + " for " +
                                 entry.name);
    }

    std::vector<uint8_t> out(entry.uncompressed_size);
    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    zs.avail_in = entry.compressed_size;
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = entry.uncompressed_size;

    int rc = inflateInit2(&zs, -MAX_WBITS);
    if (rc != Z_OK)
        throw std::runtime_error("zlib inflateInit2 failed");
    rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || zs.total_out != entry.uncompressed_size)
        throw std::runtime_error("zlib inflate failed for " + entry.name);
    return out;
}

bool starts_with(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_root_dex(const std::string& path)
{
    if (path.find('/') != std::string::npos)
        return false;
    return path == "classes.dex" ||
           (starts_with(path, "classes") && ends_with(path, ".dex"));
}

bool safe_zip_path(const std::string& path)
{
    if (path.empty() || path.front() == '/' ||
        path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) {
        return false;
    }

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        std::string part = path.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

std::string filename_from_zip_path(const std::string& path)
{
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string lib_base_name(const std::string& filename)
{
    std::string base = filename_from_zip_path(filename);
    if (starts_with(base, "lib") && ends_with(base, ".so"))
        return base.substr(3, base.size() - 6);
    if (ends_with(base, ".so"))
        return base.substr(0, base.size() - 3);
    return base;
}

bool lib_name_matches(const std::string& entry_name, const std::string& requested)
{
    std::string filename = filename_from_zip_path(entry_name);
    return requested == filename || requested == lib_base_name(filename);
}

std::string sanitize_path_component(std::string value)
{
    for (char& c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '-' && c != '_' && c != '.')
            c = '_';
    }
    if (value.empty())
        value = "apk";
    return value;
}

std::filesystem::path default_extract_dir(const std::filesystem::path& apk_path,
                                          const std::filesystem::path& base_dir)
{
    std::string seed = std::filesystem::absolute(apk_path).string();
    try {
        seed += ":" + std::to_string(std::filesystem::file_size(apk_path));
        seed += ":" + std::to_string(
            static_cast<long long>(
                std::filesystem::last_write_time(apk_path)
                    .time_since_epoch()
                    .count()));
    } catch (const std::exception&) {
        // Best-effort stable-ish path; the actual APK read will fail later if needed.
    }

    std::ostringstream hash;
    hash << std::hex << std::hash<std::string>{}(seed);
    std::string dir = sanitize_path_component(apk_path.stem().string()) +
                      "-" + hash.str();
    std::filesystem::path base = base_dir.empty()
        ? std::filesystem::current_path() / "build" / "apk"
        : base_dir;
    return base / dir;
}

std::string utf8_length_string(const std::vector<uint8_t>& data, size_t off)
{
    auto read_len = [&](size_t& pos) -> uint32_t {
        if (pos >= data.size())
            throw std::runtime_error("AXML string length is truncated");
        uint8_t first = data[pos++];
        if ((first & 0x80u) == 0)
            return first;
        if (pos >= data.size())
            throw std::runtime_error("AXML string length is truncated");
        return ((first & 0x7fu) << 8) | data[pos++];
    };

    size_t pos = off;
    (void)read_len(pos);
    uint32_t byte_len = read_len(pos);
    if (pos + byte_len > data.size())
        throw std::runtime_error("AXML UTF-8 string is truncated");
    return std::string(reinterpret_cast<const char*>(data.data() + pos),
                       byte_len);
}

std::string utf16_length_string(const std::vector<uint8_t>& data, size_t off)
{
    auto read_len = [&](size_t& pos) -> uint32_t {
        uint16_t first = read_u16(data, pos);
        pos += 2;
        if ((first & 0x8000u) == 0)
            return first;
        uint16_t second = read_u16(data, pos);
        pos += 2;
        return ((first & 0x7fffu) << 16) | second;
    };

    size_t pos = off;
    uint32_t len = read_len(pos);
    std::string out;
    out.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        uint16_t ch = read_u16(data, pos);
        pos += 2;
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

std::vector<std::string> axml_string_pool(const std::vector<uint8_t>& manifest)
{
    constexpr uint16_t RES_STRING_POOL_TYPE = 0x0001;
    constexpr uint32_t UTF8_FLAG = 0x00000100;

    std::vector<std::string> strings;
    size_t off = 0;
    if (manifest.size() >= 8 && read_u16(manifest, 0) == 0x0003)
        off = 8;

    while (off + 28 <= manifest.size()) {
        uint16_t type = read_u16(manifest, off);
        uint16_t header_size = read_u16(manifest, off + 2);
        uint32_t chunk_size = read_u32(manifest, off + 4);
        if (chunk_size < header_size || off + chunk_size > manifest.size())
            break;
        if (type != RES_STRING_POOL_TYPE) {
            off += chunk_size;
            continue;
        }

        uint32_t string_count = read_u32(manifest, off + 8);
        uint32_t flags = read_u32(manifest, off + 16);
        uint32_t strings_start = read_u32(manifest, off + 20);
        bool utf8 = (flags & UTF8_FLAG) != 0;
        if (header_size + string_count * 4u > chunk_size ||
            strings_start >= chunk_size) {
            return strings;
        }

        for (uint32_t i = 0; i < string_count; ++i) {
            uint32_t string_off = read_u32(manifest, off + header_size + i * 4u);
            size_t string_pos = off + strings_start + string_off;
            if (string_pos >= off + chunk_size)
                continue;
            try {
                strings.push_back(utf8
                    ? utf8_length_string(manifest, string_pos)
                    : utf16_length_string(manifest, string_pos));
            } catch (const std::exception&) {
                strings.push_back({});
            }
        }
        return strings;
    }
    return strings;
}

std::optional<std::string>
infer_plain_manifest_lib(const std::string& manifest,
                         const std::vector<std::string>& available_bases)
{
    if (manifest.find("android.app.lib_name") == std::string::npos)
        return std::nullopt;

    for (const std::string& base : available_bases) {
        if (manifest.find("\"" + base + "\"") != std::string::npos ||
            manifest.find("'" + base + "'") != std::string::npos)
            return base;
    }
    if (available_bases.size() == 1)
        return available_bases.front();
    return std::nullopt;
}

std::optional<std::string>
plain_manifest_attribute(const std::string& manifest,
                         const std::string& element,
                         const std::string& attr)
{
    size_t tag = manifest.find("<" + element);
    if (tag == std::string::npos)
        return std::nullopt;
    size_t end = manifest.find('>', tag);
    if (end == std::string::npos)
        return std::nullopt;

    size_t attr_pos = manifest.find(attr, tag);
    if (attr_pos == std::string::npos || attr_pos > end)
        return std::nullopt;
    size_t eq = manifest.find('=', attr_pos + attr.size());
    if (eq == std::string::npos || eq > end)
        return std::nullopt;
    size_t quote = manifest.find_first_of("\"'", eq + 1);
    if (quote == std::string::npos || quote > end)
        return std::nullopt;
    char q = manifest[quote];
    size_t close = manifest.find(q, quote + 1);
    if (close == std::string::npos || close > end)
        return std::nullopt;
    return manifest.substr(quote + 1, close - quote - 1);
}

std::optional<std::string>
tag_attribute(const std::string& tag, const std::string& attr)
{
    size_t attr_pos = tag.find(attr);
    if (attr_pos == std::string::npos)
        return std::nullopt;
    size_t eq = tag.find('=', attr_pos + attr.size());
    if (eq == std::string::npos)
        return std::nullopt;
    size_t quote = tag.find_first_of("\"'", eq + 1);
    if (quote == std::string::npos)
        return std::nullopt;
    char q = tag[quote];
    size_t close = tag.find(q, quote + 1);
    if (close == std::string::npos)
        return std::nullopt;
    return tag.substr(quote + 1, close - quote - 1);
}

std::string normalize_activity_name(const std::string& package_name,
                                    std::string activity_name)
{
    if (activity_name.empty())
        return activity_name;
    if (!package_name.empty() && activity_name.front() == '.')
        return package_name + activity_name;
    if (!package_name.empty() &&
        activity_name.find('.') == std::string::npos) {
        return package_name + "." + activity_name;
    }
    return activity_name;
}

std::optional<std::string>
infer_plain_manifest_package(const std::string& manifest)
{
    return plain_manifest_attribute(manifest, "manifest", "package");
}

std::optional<std::string>
infer_plain_manifest_launch_activity(const std::string& manifest,
                                     const std::optional<std::string>& package_name)
{
    struct ActivityCandidate {
        std::string name;
        bool is_launcher = false;
    };

    std::vector<ActivityCandidate> candidates;
    size_t pos = 0;
    while (true) {
        size_t activity_start = manifest.find("<activity", pos);
        if (activity_start == std::string::npos)
            break;
        size_t open_end = manifest.find('>', activity_start);
        if (open_end == std::string::npos)
            break;

        std::string tag = manifest.substr(activity_start, open_end - activity_start + 1);
        std::optional<std::string> name =
            tag_attribute(tag, "android:name");
        if (!name)
            name = tag_attribute(tag, "name");

        size_t activity_end = manifest.find("</activity>", open_end);
        size_t block_end = activity_end == std::string::npos
            ? open_end
            : activity_end + std::string("</activity>").size();
        std::string block = manifest.substr(
            activity_start, block_end - activity_start);

        if (name) {
            ActivityCandidate candidate;
            candidate.name = normalize_activity_name(
                package_name.value_or(std::string()), *name);
            candidate.is_launcher =
                block.find("android.intent.action.MAIN") != std::string::npos &&
                block.find("android.intent.category.LAUNCHER") != std::string::npos;
            candidates.push_back(std::move(candidate));
        }

        pos = block_end;
    }

    for (const auto& candidate : candidates) {
        if (candidate.is_launcher)
            return candidate.name;
    }
    if (!candidates.empty())
        return candidates.front().name;
    return std::nullopt;
}

std::optional<std::string>
infer_binary_manifest_lib(const std::vector<uint8_t>& manifest,
                          const std::vector<std::string>& available_bases)
{
    std::vector<std::string> strings = axml_string_pool(manifest);
    bool has_lib_marker = false;
    for (const std::string& value : strings) {
        if (value == "android.app.lib_name") {
            has_lib_marker = true;
            break;
        }
    }
    if (!has_lib_marker)
        return std::nullopt;

    for (const std::string& base : available_bases) {
        if (std::find(strings.begin(), strings.end(), base) != strings.end())
            return base;
    }
    if (available_bases.size() == 1)
        return available_bases.front();
    return std::nullopt;
}

std::optional<std::string>
infer_binary_manifest_package(const std::vector<uint8_t>& manifest)
{
    constexpr uint16_t RES_XML_START_ELEMENT_TYPE = 0x0102;
    constexpr uint32_t NO_INDEX = 0xffffffffu;
    constexpr uint8_t TYPE_STRING = 0x03;

    std::vector<std::string> strings = axml_string_pool(manifest);
    if (strings.empty())
        return std::nullopt;

    size_t off = 0;
    if (manifest.size() >= 8 && read_u16(manifest, 0) == 0x0003)
        off = 8;

    while (off + 8 <= manifest.size()) {
        uint16_t type = read_u16(manifest, off);
        uint16_t header_size = read_u16(manifest, off + 2);
        uint32_t chunk_size = read_u32(manifest, off + 4);
        if (chunk_size < header_size || off + chunk_size > manifest.size())
            break;

        if (type == RES_XML_START_ELEMENT_TYPE && off + 36 <= manifest.size()) {
            uint32_t elem_name_idx = read_u32(manifest, off + 20);
            if (elem_name_idx < strings.size() && strings[elem_name_idx] == "manifest") {
                uint16_t attr_start = read_u16(manifest, off + 24);
                uint16_t attr_size = read_u16(manifest, off + 26);
                uint16_t attr_count = read_u16(manifest, off + 28);
                size_t attrs = off + 16u + attr_start;
                for (uint16_t i = 0; i < attr_count; ++i) {
                    size_t a = attrs + static_cast<size_t>(i) * attr_size;
                    if (a + 20 > off + chunk_size)
                        break;
                    uint32_t name_idx = read_u32(manifest, a + 4);
                    if (name_idx >= strings.size() || strings[name_idx] != "package")
                        continue;

                    uint32_t raw_idx = read_u32(manifest, a + 8);
                    if (raw_idx != NO_INDEX && raw_idx < strings.size())
                        return strings[raw_idx];
                    uint8_t data_type = manifest[a + 15];
                    uint32_t data = read_u32(manifest, a + 16);
                    if (data_type == TYPE_STRING && data < strings.size())
                        return strings[data];
                }
            }
        }

        off += chunk_size;
    }

    return std::nullopt;
}

struct ManifestInfo {
    std::optional<std::string> lib_name;
    std::optional<std::string> package_name;
    std::optional<std::string> launch_activity;
};

ManifestInfo infer_manifest_info(const std::vector<uint8_t>& manifest,
                                 const std::vector<std::string>& available_bases)
{
    auto first_nonspace = std::find_if(manifest.begin(), manifest.end(),
        [](uint8_t c) { return !std::isspace(static_cast<unsigned char>(c)); });
    if (first_nonspace != manifest.end() && *first_nonspace == '<') {
        std::string text(reinterpret_cast<const char*>(manifest.data()),
                         manifest.size());
        auto package_name = infer_plain_manifest_package(text);
        return {
            infer_plain_manifest_lib(text, available_bases),
            package_name,
            infer_plain_manifest_launch_activity(text, package_name)
        };
    }
    auto package_name = infer_binary_manifest_package(manifest);
    return {
        infer_binary_manifest_lib(manifest, available_bases),
        package_name,
        std::nullopt
    };
}

std::string join_libs(const std::vector<std::string>& libs)
{
    std::string out;
    for (size_t i = 0; i < libs.size(); ++i) {
        if (i) out += ", ";
        out += filename_from_zip_path(libs[i]);
    }
    return out;
}

ApkRuntimeKind runtime_kind_for(const std::vector<std::string>& arm64_libs,
                                const std::vector<std::string>& dex_files)
{
    if (!arm64_libs.empty() && !dex_files.empty())
        return ApkRuntimeKind::Mixed;
    if (!arm64_libs.empty())
        return ApkRuntimeKind::NativeOnly;
    if (!dex_files.empty())
        return ApkRuntimeKind::JavaOnly;
    return ApkRuntimeKind::Empty;
}

ApkClassification classify_entries(const std::filesystem::path& apk_path,
                                   const std::vector<uint8_t>& apk,
                                   const std::vector<ZipEntry>& entries)
{
    ApkClassification classification;
    classification.apk_path = apk_path;

    const ZipEntry* manifest_entry = nullptr;
    for (const ZipEntry& entry : entries) {
        if (entry.name == "AndroidManifest.xml") {
            manifest_entry = &entry;
            classification.has_manifest = true;
        } else if (starts_with(entry.name, "lib/arm64-v8a/") &&
                   ends_with(entry.name, ".so")) {
            classification.arm64_libs.push_back(entry.name);
        } else if (is_root_dex(entry.name)) {
            classification.dex_files.push_back(entry.name);
        } else if (starts_with(entry.name, "assets/") &&
                   !ends_with(entry.name, "/") &&
                   safe_zip_path(entry.name)) {
            classification.asset_entries.push_back(entry.name);
        }
    }

    std::vector<std::string> available_bases;
    for (const std::string& lib : classification.arm64_libs)
        available_bases.push_back(lib_base_name(lib));

    if (manifest_entry) {
        std::vector<uint8_t> manifest = extract_entry_data(apk, *manifest_entry);
        ManifestInfo manifest_info = infer_manifest_info(manifest, available_bases);
        classification.manifest_lib = manifest_info.lib_name;
        classification.manifest_package = manifest_info.package_name;
        classification.manifest_launch_activity =
            manifest_info.launch_activity;
    }

    std::sort(classification.arm64_libs.begin(),
              classification.arm64_libs.end());
    std::sort(classification.dex_files.begin(), classification.dex_files.end());
    std::sort(classification.asset_entries.begin(),
              classification.asset_entries.end());
    classification.runtime_kind =
        runtime_kind_for(classification.arm64_libs, classification.dex_files);
    return classification;
}

} // namespace

std::string to_string(ApkRuntimeKind kind)
{
    switch (kind) {
    case ApkRuntimeKind::Empty:
        return "empty";
    case ApkRuntimeKind::NativeOnly:
        return "native-only";
    case ApkRuntimeKind::JavaOnly:
        return "java-only";
    case ApkRuntimeKind::Mixed:
        return "mixed";
    }
    return "empty";
}

ApkClassification classify_apk(const std::filesystem::path& apk_path)
{
    if (apk_path.empty())
        throw std::runtime_error("APK path is empty");

    std::vector<uint8_t> apk = read_file(apk_path);
    std::vector<ZipEntry> entries = read_zip_entries(apk);
    return classify_entries(apk_path, apk, entries);
}

ApkLaunchResult prepare_apk_launch(const ApkLaunchConfig& config)
{
    if (config.apk_path.empty())
        throw std::runtime_error("APK path is empty");

    std::vector<uint8_t> apk = read_file(config.apk_path);
    std::vector<ZipEntry> entries = read_zip_entries(apk);

    ApkClassification classification =
        classify_entries(config.apk_path, apk, entries);

    std::vector<ZipEntry> arm64_libs;
    std::vector<ZipEntry> asset_entries;
    for (const ZipEntry& entry : entries) {
        if (starts_with(entry.name, "lib/arm64-v8a/") &&
                   ends_with(entry.name, ".so")) {
            arm64_libs.push_back(entry);
        } else if (starts_with(entry.name, "assets/") &&
                   !ends_with(entry.name, "/") &&
                   safe_zip_path(entry.name)) {
            asset_entries.push_back(entry);
        }
    }

    if (arm64_libs.empty() &&
        classification.runtime_kind == ApkRuntimeKind::JavaOnly) {
        throw std::runtime_error(
            "APK runtime kind is java-only; Java/ART APK launch is not "
            "implemented yet");
    }

    if (arm64_libs.empty())
        throw std::runtime_error("APK has no lib/arm64-v8a/*.so entries");

    std::vector<std::string> available_bases;
    for (const ZipEntry& lib : arm64_libs)
        available_bases.push_back(lib_base_name(lib.name));

    std::optional<std::string> manifest_lib = classification.manifest_lib;
    std::optional<std::string> manifest_package =
        classification.manifest_package;
    std::optional<std::string> manifest_launch_activity =
        classification.manifest_launch_activity;

    const ZipEntry* selected = nullptr;
    if (config.lib_name) {
        for (const ZipEntry& lib : arm64_libs) {
            if (lib_name_matches(lib.name, *config.lib_name)) {
                selected = &lib;
                break;
            }
        }
        if (!selected) {
            throw std::runtime_error("APK does not contain requested arm64 lib '" +
                                     *config.lib_name + "'; available: " +
                                     join_libs(available_bases));
        }
    } else if (manifest_lib) {
        for (const ZipEntry& lib : arm64_libs) {
            if (lib_name_matches(lib.name, *manifest_lib)) {
                selected = &lib;
                break;
            }
        }
    } else if (arm64_libs.size() == 1) {
        selected = &arm64_libs.front();
    } else {
        for (const ZipEntry& lib : arm64_libs) {
            if (lib_name_matches(lib.name, "main")) {
                selected = &lib;
                break;
            }
        }
    }

    if (!selected) {
        throw std::runtime_error(
            "APK has multiple arm64 native libraries and no clear NativeActivity "
            "lib_name; use --apk-lib. Available: " + join_libs(available_bases));
    }

    std::filesystem::path extract_dir = config.output_dir.empty()
        ? default_extract_dir(config.apk_path, config.output_base_dir)
        : config.output_dir;
    std::filesystem::path lib_dir = extract_dir / "lib" / "arm64-v8a";
    std::filesystem::path assets_dir = extract_dir / "assets";
    std::filesystem::create_directories(lib_dir);
    if (!asset_entries.empty())
        std::filesystem::create_directories(assets_dir);

    ApkLaunchResult result;
    result.apk_path = config.apk_path;
    result.extract_dir = extract_dir;
    result.assets_dir = assets_dir;
    result.selected_lib = lib_base_name(selected->name);
    result.manifest_lib = manifest_lib;
    result.manifest_package = manifest_package;
    result.manifest_launch_activity = manifest_launch_activity;

    for (const ZipEntry& lib : arm64_libs) {
        std::vector<uint8_t> bytes = extract_entry_data(apk, lib);
        std::string filename = filename_from_zip_path(lib.name);
        std::filesystem::path out_path = lib_dir / filename;
        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("unable to write extracted lib: " +
                                     out_path.string());
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        result.extracted_libs.push_back(out_path.string());
        if (lib.name == selected->name)
            result.so_path = out_path;
    }

    for (const ZipEntry& asset : asset_entries) {
        std::vector<uint8_t> bytes = extract_entry_data(apk, asset);
        std::filesystem::path out_path = extract_dir / asset.name;
        std::filesystem::create_directories(out_path.parent_path());

        std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error("unable to write extracted asset: " +
                                     out_path.string());
        if (!bytes.empty())
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        result.extracted_assets.push_back(out_path.string());
    }

    if (result.so_path.empty())
        throw std::runtime_error("internal APK extraction error: selected lib missing");
    return result;
}

} // namespace muplar::runtime::apk
