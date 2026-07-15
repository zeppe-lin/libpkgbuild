#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

class CurlDownloader final : public Downloader {
public:
    std::string_view name() const noexcept override { return "libcurl"; }
    DownloadReceipt fetch(const DownloadRequest& request,
                          EventSink& events) const override;
};

} // namespace pkgbuild
