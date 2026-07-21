#pragma once

#include <pkgbuild/backend.hpp>
#include <pkgbuild/process.hpp>

namespace pkgbuild {

class PkgfileDefinitionLoader final : public DefinitionLoader {
public:
    PkgfileDefinitionLoader(std::filesystem::path helper,
                            const ProcessExecutor& processes)
        : helper_(std::move(helper)), processes_(processes) {}

    std::string_view name() const noexcept override { return "pkgfile/0"; }
    PackageDefinition load(const DefinitionRequest& request,
                           EventSink& events) const override;

private:
    std::filesystem::path helper_;
    const ProcessExecutor& processes_;
};

class PosixShellRecipeRunner final : public RecipeRunner {
public:
    PosixShellRecipeRunner(std::filesystem::path helper,
                           const ProcessExecutor& processes)
        : helper_(std::move(helper)), processes_(processes) {}

    std::string_view name() const noexcept override { return "pkgfile/0-shell"; }
    StagedPackage run(const RecipeRequest& request,
                      EventSink& events) const override;
    StagedPackage run_captured(const CapturedRecipeRequest& request,
                               EventSink& events) const override;

private:
    std::filesystem::path helper_;
    const ProcessExecutor& processes_;
};

} // namespace pkgbuild
