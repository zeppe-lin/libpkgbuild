#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

class OpenSslSourceVerifier final : public SourceVerifier {
public:
    std::string_view name() const noexcept override { return "OpenSSL EVP"; }

    VerifiedSource verify(const std::filesystem::path& source,
                          const std::vector<Digest>& digests,
                          EventSink& events) const override;

    void revalidate(const VerifiedSource& source,
                    EventSink& events) const override;
};

} // namespace pkgbuild
