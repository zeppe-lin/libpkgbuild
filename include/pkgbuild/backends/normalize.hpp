#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

class PackageTreeTransformer final : public PackageTransformer {
public:
    std::string_view name() const noexcept override
    {
        return "package-tree";
    }

    TransformationReceipt transform(
        const PackageTransformRequest& request,
        EventSink& events) const override;
};

} // namespace pkgbuild
