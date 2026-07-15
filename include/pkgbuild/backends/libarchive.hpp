#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

class LibarchiveBackend final : public SourceExtractor,
                                public PackageWriter {
public:
    std::string_view name() const noexcept override { return "libarchive"; }

    void extract(const ExtractRequest& request,
                 EventSink& events) const override;

    bool supports(const ArchiveSpec& archive) const noexcept override;

    ArchiveReceipt write(const PackageWriteRequest& request,
                         EventSink& events) const override;
};

} // namespace pkgbuild
