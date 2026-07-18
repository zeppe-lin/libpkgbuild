#include "archive_compare.hpp"

#include <libpkgimage/entry_selection.h>
#include <libpkgimage/libarchive_backend.h>
#include <libpkgimage/package_archive.h>
#include <libpkgimage/payload_sink.h>

#include <openssl/evp.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pkgbuild::parity {
namespace {

struct EvpContextDeleter {
    void operator()(EVP_MD_CTX* value) const noexcept
    {
        EVP_MD_CTX_free(value);
    }
};

using EvpContext = std::unique_ptr<EVP_MD_CTX, EvpContextDeleter>;

std::string hex_digest(const unsigned char* data, unsigned int size)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i != size; ++i)
        output << std::setw(2) << static_cast<unsigned int>(data[i]);
    return output.str();
}

struct PayloadDigest {
    std::uint64_t size{0};
    std::string sha256;
};

bool ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_compressed_manpage(const std::string& value)
{
    const std::filesystem::path path(value);
    std::vector<std::string> components;
    for (const auto& component : path)
        components.push_back(component.string());

    if (components.size() < 3 || !ends_with(components.back(), ".gz"))
        return false;

    for (std::size_t index = 0; index + 2 < components.size(); ++index) {
        if (components[index] == "man" &&
            components[index + 1].rfind("man", 0) == 0 &&
            components[index + 1].size() > 3)
            return true;
    }
    return false;
}

class PayloadHasher final : public pkgimage::payload_sink {
public:
    ~PayloadHasher() override
    {
        reset_inflater();
    }

    void begin(const pkgimage::package_entry& entry) override
    {
        current_path_ = entry.path.string();
        normalize_gzip_ = is_compressed_manpage(current_path_);
        normalize_ar_ = ends_with(current_path_, ".a");
        semantic_size_ = 0;
        gzip_finished_ = false;

        context_.reset(EVP_MD_CTX_new());
        if (!context_ ||
            EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("cannot initialize SHA-256 payload digest");

        if (normalize_gzip_) {
            stream_ = {};
            if (inflateInit2(&stream_, 15 + 16) != Z_OK)
                throw std::runtime_error("cannot initialize gzip payload decoder");
            inflater_initialized_ = true;
        }
        if (normalize_ar_) {
            ar_state_ = ArState::magic;
            ar_buffer_size_ = 0;
            ar_payload_remaining_ = 0;
        }
    }

    void write(const pkgimage::package_entry&,
               const std::byte* data,
               std::size_t size) override
    {
        if (normalize_ar_) {
            consume_ar(data, size);
            return;
        }
        if (!normalize_gzip_) {
            update_digest(data, size);
            semantic_size_ += size;
            return;
        }

        if (gzip_finished_ && size != 0)
            throw std::runtime_error("gzip man page contains trailing data: '" +
                                     current_path_ + "'");

        std::size_t offset = 0;
        while (offset != size) {
            const auto chunk = std::min<std::size_t>(
                size - offset, std::numeric_limits<uInt>::max());
            stream_.next_in = reinterpret_cast<Bytef*>(
                const_cast<std::byte*>(data + offset));
            stream_.avail_in = static_cast<uInt>(chunk);

            while (stream_.avail_in != 0) {
                std::array<unsigned char, 64 * 1024> output {};
                stream_.next_out = output.data();
                stream_.avail_out = static_cast<uInt>(output.size());
                const auto before = stream_.avail_in;
                const int result = inflate(&stream_, Z_NO_FLUSH);
                const std::size_t produced = output.size() - stream_.avail_out;
                update_digest(reinterpret_cast<const std::byte*>(output.data()),
                              produced);
                semantic_size_ += produced;

                if (result == Z_STREAM_END) {
                    gzip_finished_ = true;
                    if (stream_.avail_in != 0 || offset + chunk != size)
                        throw std::runtime_error(
                            "gzip man page contains trailing data: '" +
                            current_path_ + "'");
                    break;
                }
                if (result != Z_OK ||
                    (produced == 0 && stream_.avail_in == before))
                    throw std::runtime_error("invalid gzip man page payload: '" +
                                             current_path_ + "'");
            }
            offset += chunk;
        }
    }

    void end(const pkgimage::package_entry&) override
    {
        if (normalize_gzip_) {
            if (!gzip_finished_)
                throw std::runtime_error("truncated gzip man page payload: '" +
                                         current_path_ + "'");
            reset_inflater();
        }
        if (normalize_ar_ &&
            (ar_state_ != ArState::header || ar_buffer_size_ != 0))
            throw std::runtime_error("truncated ar archive payload: '" +
                                     current_path_ + "'");

        std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1)
            throw std::runtime_error("cannot finalize SHA-256 payload digest");
        payloads_.emplace(current_path_,
                          PayloadDigest{semantic_size_,
                                        hex_digest(digest.data(), size)});
        context_.reset();
        current_path_.clear();
        normalize_gzip_ = false;
        normalize_ar_ = false;
        semantic_size_ = 0;
        gzip_finished_ = false;
    }

    const std::map<std::string, PayloadDigest>& payloads() const noexcept
    {
        return payloads_;
    }

private:
    enum class ArState {
        magic,
        header,
        payload,
        padding,
    };

    static std::uint64_t parse_ar_size(const std::array<std::byte, 60>& header,
                                       const std::string& path)
    {
        const char* begin = reinterpret_cast<const char*>(header.data()) + 48;
        const char* end = begin + 10;
        while (begin != end && *begin == ' ')
            ++begin;
        while (end != begin && end[-1] == ' ')
            --end;
        if (begin == end)
            throw std::runtime_error("empty ar member size in '" + path + "'");

        std::uint64_t result = 0;
        const auto parsed = std::from_chars(begin, end, result, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
            throw std::runtime_error("invalid ar member size in '" + path + "'");
        return result;
    }

    void consume_ar(const std::byte* data, std::size_t size)
    {
        std::size_t offset = 0;
        while (offset != size) {
            if (ar_state_ == ArState::magic) {
                const auto count = std::min(size - offset,
                                            std::size_t{8} - ar_buffer_size_);
                std::copy_n(data + offset, count,
                            ar_buffer_.begin() + ar_buffer_size_);
                ar_buffer_size_ += count;
                offset += count;
                if (ar_buffer_size_ != 8)
                    continue;

                static constexpr std::array<std::byte, 8> ordinary = {
                    std::byte{'!'}, std::byte{'<'}, std::byte{'a'}, std::byte{'r'},
                    std::byte{'c'}, std::byte{'h'}, std::byte{'>'}, std::byte{'\n'},
                };
                static constexpr std::array<std::byte, 8> thin = {
                    std::byte{'!'}, std::byte{'<'}, std::byte{'t'}, std::byte{'h'},
                    std::byte{'i'}, std::byte{'n'}, std::byte{'>'}, std::byte{'\n'},
                };
                if (std::equal(ar_buffer_.begin(), ar_buffer_.begin() + 8,
                               thin.begin()))
                    throw std::runtime_error("thin ar archive is unsupported: '" +
                                             current_path_ + "'");
                if (!std::equal(ar_buffer_.begin(), ar_buffer_.begin() + 8,
                                ordinary.begin()))
                    throw std::runtime_error("invalid ar archive magic: '" +
                                             current_path_ + "'");
                update_digest(ar_buffer_.data(), 8);
                semantic_size_ += 8;
                ar_buffer_size_ = 0;
                ar_state_ = ArState::header;
                continue;
            }

            if (ar_state_ == ArState::header) {
                const auto count = std::min(size - offset,
                                            std::size_t{60} - ar_buffer_size_);
                std::copy_n(data + offset, count,
                            ar_buffer_.begin() + ar_buffer_size_);
                ar_buffer_size_ += count;
                offset += count;
                if (ar_buffer_size_ != 60)
                    continue;
                if (ar_buffer_[58] != std::byte{'`'} ||
                    ar_buffer_[59] != std::byte{'\n'})
                    throw std::runtime_error("invalid ar member header: '" +
                                             current_path_ + "'");

                ar_payload_remaining_ = parse_ar_size(ar_buffer_, current_path_);
                std::fill(ar_buffer_.begin() + 16, ar_buffer_.begin() + 28,
                          std::byte{' '});
                update_digest(ar_buffer_.data(), 60);
                semantic_size_ += 60;
                ar_buffer_size_ = 0;
                ar_member_odd_ = (ar_payload_remaining_ & 1U) != 0;
                ar_state_ = ar_payload_remaining_ == 0
                    ? (ar_member_odd_ ? ArState::padding : ArState::header)
                    : ArState::payload;
                continue;
            }

            if (ar_state_ == ArState::payload) {
                const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                    size - offset, ar_payload_remaining_));
                update_digest(data + offset, count);
                semantic_size_ += count;
                offset += count;
                ar_payload_remaining_ -= count;
                if (ar_payload_remaining_ == 0)
                    ar_state_ = ar_member_odd_ ? ArState::padding
                                              : ArState::header;
                continue;
            }

            update_digest(data + offset, 1);
            ++semantic_size_;
            ++offset;
            ar_state_ = ArState::header;
        }
    }

    void update_digest(const std::byte* data, std::size_t size)
    {
        if (size != 0 && EVP_DigestUpdate(context_.get(), data, size) != 1)
            throw std::runtime_error("cannot update SHA-256 payload digest");
    }

    void reset_inflater() noexcept
    {
        if (inflater_initialized_)
            (void)inflateEnd(&stream_);
        stream_ = {};
        inflater_initialized_ = false;
    }

    EvpContext context_;
    z_stream stream_ {};
    bool inflater_initialized_{false};
    bool normalize_gzip_{false};
    bool normalize_ar_{false};
    bool gzip_finished_{false};
    ArState ar_state_{ArState::magic};
    std::array<std::byte, 60> ar_buffer_{};
    std::size_t ar_buffer_size_{0};
    std::uint64_t ar_payload_remaining_{0};
    bool ar_member_odd_{false};
    std::uint64_t semantic_size_{0};
    std::string current_path_;
    std::map<std::string, PayloadDigest> payloads_;
};

std::string entry_type_name(pkgimage::entry_type type)
{
    switch (type) {
    case pkgimage::entry_type::regular: return "regular";
    case pkgimage::entry_type::directory: return "directory";
    case pkgimage::entry_type::symlink: return "symlink";
    case pkgimage::entry_type::hardlink: return "regular";
    case pkgimage::entry_type::fifo: return "fifo";
    case pkgimage::entry_type::character_device: return "character-device";
    case pkgimage::entry_type::block_device: return "block-device";
    }
    throw std::runtime_error("unknown package entry type");
}

std::string octal_mode(std::uint32_t mode)
{
    std::ostringstream output;
    output << '0' << std::oct << mode;
    return output.str();
}

std::string device_text(const std::optional<pkgimage::device_number>& device)
{
    if (!device)
        return {};
    return std::to_string(device->major) + ":" +
           std::to_string(device->minor);
}

std::string join_paths(const std::vector<std::string>& paths)
{
    std::ostringstream output;
    for (std::size_t i = 0; i != paths.size(); ++i) {
        if (i != 0)
            output << ',';
        output << paths[i];
    }
    return output.str();
}

struct SemanticEntry {
    std::string type;
    std::uint32_t mode{0};
    std::uint64_t uid{0};
    std::uint64_t gid{0};
    std::uint64_t size{0};
    std::string symlink_target;
    std::string hardlink_group;
    std::string device;
    std::string payload_sha256;
};

using SemanticArchive = std::map<std::string, SemanticEntry>;

std::map<std::string, std::vector<std::string>> hardlink_groups(
    const pkgimage::package_image& image)
{
    std::map<std::string, std::vector<std::string>> by_target;
    for (const auto& entry : image.entries()) {
        if (entry.type == pkgimage::entry_type::regular)
            by_target[entry.path.string()].push_back(entry.path.string());
    }
    for (const auto& entry : image.entries()) {
        if (entry.type != pkgimage::entry_type::hardlink)
            continue;
        by_target[entry.hardlink_target->string()].push_back(entry.path.string());
    }

    std::map<std::string, std::vector<std::string>> by_member;
    for (auto& item : by_target) {
        auto& members = item.second;
        if (members.size() < 2)
            continue;
        std::sort(members.begin(), members.end());
        for (const auto& member : members)
            by_member.emplace(member, members);
    }
    return by_member;
}

SemanticArchive inspect_archive(const std::filesystem::path& path)
{
    pkgimage::libarchive_backend backend;
    auto archive = backend.open(path);
    const auto& image = archive->image();

    PayloadHasher hasher;
    archive->replay(pkgimage::entry_selection::all_regular(image), hasher);

    const auto groups = hardlink_groups(image);
    SemanticArchive result;
    for (const auto& entry : image.entries()) {
        const pkgimage::package_entry* metadata = &entry;
        std::string payload_path = entry.path.string();
        if (entry.type == pkgimage::entry_type::hardlink) {
            metadata = image.find(*entry.hardlink_target);
            if (metadata == nullptr)
                throw std::runtime_error("hardlink target disappeared during inspection");
            payload_path = metadata->path.string();
        }

        SemanticEntry normalized;
        normalized.type = entry_type_name(entry.type);
        normalized.mode = metadata->mode;
        normalized.uid = metadata->uid;
        normalized.gid = metadata->gid;
        normalized.size = metadata->size;
        if (entry.symlink_target)
            normalized.symlink_target = *entry.symlink_target;
        const auto group = groups.find(entry.path.string());
        if (group != groups.end())
            normalized.hardlink_group = join_paths(group->second);
        normalized.device = device_text(metadata->device);
        if (metadata->type == pkgimage::entry_type::regular) {
            const auto payload = hasher.payloads().find(payload_path);
            if (payload == hasher.payloads().end())
                throw std::runtime_error("regular payload digest is absent for '" +
                                         payload_path + "'");
            normalized.size = payload->second.size;
            normalized.payload_sha256 = payload->second.sha256;
        }
        result.emplace(entry.path.string(), std::move(normalized));
    }
    return result;
}

void add_difference(ArchiveComparison& report,
                    const std::string& path,
                    const std::string& field,
                    std::string reference,
                    std::string candidate)
{
    report.differences.push_back(
        ArchiveDifference{path, field, std::move(reference),
                          std::move(candidate)});
}

template <typename T>
void compare_field(ArchiveComparison& report,
                   const std::string& path,
                   const std::string& field,
                   const T& reference,
                   const T& candidate)
{
    if (reference == candidate)
        return;
    std::ostringstream left;
    std::ostringstream right;
    left << reference;
    right << candidate;
    add_difference(report, path, field, left.str(), right.str());
}

void compare_entry(ArchiveComparison& report,
                   const std::string& path,
                   const SemanticEntry& reference,
                   const SemanticEntry& candidate)
{
    compare_field(report, path, "type", reference.type, candidate.type);
    if (reference.mode != candidate.mode)
        add_difference(report, path, "mode", octal_mode(reference.mode),
                       octal_mode(candidate.mode));
    compare_field(report, path, "uid", reference.uid, candidate.uid);
    compare_field(report, path, "gid", reference.gid, candidate.gid);
    compare_field(report, path, "size", reference.size, candidate.size);
    compare_field(report, path, "symlink-target", reference.symlink_target,
                  candidate.symlink_target);
    compare_field(report, path, "hardlink-group", reference.hardlink_group,
                  candidate.hardlink_group);
    compare_field(report, path, "device", reference.device, candidate.device);
    compare_field(report, path, "payload-sha256", reference.payload_sha256,
                  candidate.payload_sha256);
}

} // namespace

ArchiveComparison compare_archives(
    const std::filesystem::path& reference,
    const std::filesystem::path& candidate)
{
    ArchiveComparison report{reference, candidate, {}};
    const auto reference_image = inspect_archive(reference);
    const auto candidate_image = inspect_archive(candidate);

    for (const auto& item : reference_image) {
        const auto found = candidate_image.find(item.first);
        if (found == candidate_image.end()) {
            add_difference(report, item.first, "presence", "present", "absent");
            continue;
        }
        compare_entry(report, item.first, item.second, found->second);
    }
    for (const auto& item : candidate_image) {
        if (reference_image.find(item.first) == reference_image.end())
            add_difference(report, item.first, "presence", "absent", "present");
    }
    return report;
}

std::string format_difference(const ArchiveDifference& difference)
{
    return difference.path + ": " + difference.field + ": " +
           difference.reference + " -> " + difference.candidate;
}

} // namespace pkgbuild::parity
