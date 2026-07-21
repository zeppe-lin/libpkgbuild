#pragma once

#include <pkgbuild/backend.hpp>
#include <pkgbuild/process.hpp>

namespace pkgbuild {

class PackageTreeTransformer final : public PackageTransformer {
public:
    PackageTreeTransformer(std::filesystem::path strip_program,
                           const ProcessExecutor& processes)
        : strip_program_(std::move(strip_program)), processes_(processes) {}

    std::string_view name() const noexcept override
    {
        return "package-tree";
    }

    TransformationReceipt transform(
        const PackageTransformRequest& request,
        EventSink& events) const override;
    TransformationReceipt transform_definition(
        const DefinitionTransformRequest& request,
        EventSink& events) const override;

private:
    std::filesystem::path strip_program_;
    const ProcessExecutor& processes_;
};

} // namespace pkgbuild
