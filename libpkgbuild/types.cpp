#include <pkgbuild/error.hpp>
#include <pkgbuild/types.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace pkgbuild {

std::string to_string(ArchiveFormat format)
{
    switch (format) {
    case ArchiveFormat::gnutar: return "gnutar";
    case ArchiveFormat::pax: return "pax";
    case ArchiveFormat::ustar: return "ustar";
    case ArchiveFormat::v7: return "v7";
    }
    throw Error(ErrorCode::invalid_configuration, "unknown archive format");
}

std::string to_string(Compression compression)
{
    switch (compression) {
    case Compression::gzip: return "gz";
    case Compression::bzip2: return "bz2";
    case Compression::xz: return "xz";
    case Compression::lzip: return "lz";
    case Compression::zstd: return "zst";
    }
    throw Error(ErrorCode::invalid_configuration, "unknown compression mode");
}


std::string to_string(DigestAlgorithm algorithm)
{
    switch (algorithm) {
    case DigestAlgorithm::md5: return "md5";
    case DigestAlgorithm::sha256: return "sha256";
    case DigestAlgorithm::sha512: return "sha512";
    case DigestAlgorithm::blake2b512: return "blake2b512";
    }
    throw Error(ErrorCode::invalid_configuration, "unknown digest algorithm");
}

ArchiveFormat archive_format_from_string(const std::string& value)
{
    if (value == "gnutar") return ArchiveFormat::gnutar;
    if (value == "pax") return ArchiveFormat::pax;
    if (value == "ustar") return ArchiveFormat::ustar;
    if (value == "v7") return ArchiveFormat::v7;
    throw Error(ErrorCode::invalid_configuration,
                "unsupported archive format: " + value);
}

Compression compression_from_string(const std::string& value)
{
    if (value == "gz") return Compression::gzip;
    if (value == "bz2") return Compression::bzip2;
    if (value == "xz") return Compression::xz;
    if (value == "lz") return Compression::lzip;
    if (value == "zst") return Compression::zstd;
    throw Error(ErrorCode::invalid_configuration,
                "unsupported compression mode: " + value);
}

std::string package_extension(const ArchiveSpec& archive)
{
    return ".pkg.tar." + to_string(archive.compression);
}

std::filesystem::path package_filename(const PackageDefinition& definition)
{
    return definition.id.name + "#" + definition.id.version + "-" +
           definition.id.release + package_extension(definition.archive);
}

namespace {

bool has_uri_scheme(const std::string& value)
{
    return value.rfind("http://", 0) == 0 ||
           value.rfind("https://", 0) == 0 ||
           value.rfind("ftp://", 0) == 0 ||
           value.rfind("file://", 0) == 0;
}

std::filesystem::path uri_basename(std::string uri)
{
    const auto suffix = uri.find_first_of("?#");
    if (suffix != std::string::npos)
        uri.erase(suffix);
    const auto slash = uri.find_last_of('/');
    if (slash == std::string::npos || slash + 1 == uri.size())
        throw Error(ErrorCode::invalid_definition,
                    "source URI has no local filename: " + uri);
    return uri.substr(slash + 1);
}

bool ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

Source parse_source(const std::string& declaration)
{
    Source source;
    source.declaration = declaration;

    const auto rename = declaration.find("::");
    if (rename != std::string::npos) {
        const std::string local = declaration.substr(0, rename);
        const std::string uri = declaration.substr(rename + 2);
        if (local.empty() || !has_uri_scheme(uri))
            throw Error(ErrorCode::invalid_definition,
                        "invalid renamed source: " + declaration);
        source.local_name = local;
        source.uri = uri;
        return source;
    }

    if (has_uri_scheme(declaration)) {
        source.uri = declaration;
        source.local_name = uri_basename(declaration);
        return source;
    }

    source.local_name = declaration;
    return source;
}

bool source_is_archive(const std::filesystem::path& path)
{
    const std::string value = path.filename().string();
    static const std::vector<std::string> suffixes = {
        ".tar", ".tar.gz", ".tar.Z", ".tgz", ".tar.bz2", ".tbz2",
        ".tar.xz", ".txz", ".tar.lzma", ".tar.lz", ".tar.zst",
        ".tzst", ".zst", ".zip", ".rpm", ".7z",
    };
    return std::any_of(suffixes.begin(), suffixes.end(),
                       [&](const auto& suffix) { return ends_with(value, suffix); });
}

} // namespace pkgbuild
