#pragma once

#include <pkgbuild/backend.hpp>

namespace pkgbuild {

class PkgfileDefinitionLoader final : public DefinitionLoader {
public:
    explicit PkgfileDefinitionLoader(std::filesystem::path helper)
        : helper_(std::move(helper)) {}

    std::string_view name() const noexcept override { return "pkgfile/0"; }
    PackageDefinition load(const DefinitionRequest& request,
                           EventSink& events) const override;

private:
    std::filesystem::path helper_;
};

class PosixShellRecipeRunner final : public RecipeRunner {
public:
    explicit PosixShellRecipeRunner(std::filesystem::path helper)
        : helper_(std::move(helper)) {}

    std::string_view name() const noexcept override { return "pkgfile/0-shell"; }
    void run(const RecipeRequest& request,
             EventSink& events) const override;

private:
    std::filesystem::path helper_;
};

} // namespace pkgbuild
