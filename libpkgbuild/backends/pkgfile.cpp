#include <pkgbuild/backends/pkgfile.hpp>
#include <pkgbuild/error.hpp>

#include "../process.hpp"

#include <sstream>

namespace pkgbuild {
namespace {

std::string optional_path(const std::optional<std::filesystem::path>& path)
{
    return path ? std::filesystem::absolute(*path).string() : std::string{};
}

std::vector<std::string> split_sources(const std::string& value)
{
    std::istringstream input(value);
    std::vector<std::string> sources;
    std::string item;
    while (input >> item)
        sources.push_back(std::move(item));
    return sources;
}

std::vector<std::string> common_arguments(const DefinitionRequest& request)
{
    return {
        std::filesystem::absolute(request.paths.recipe_dir).string(),
        optional_path(request.config_file),
        std::filesystem::absolute(request.paths.source_dir).string(),
        std::filesystem::absolute(request.paths.package_dir).string(),
        std::filesystem::absolute(request.paths.work_dir).string(),
        to_string(request.defaults.format),
        to_string(request.defaults.compression),
    };
}

} // namespace

PackageDefinition PkgfileDefinitionLoader::load(const DefinitionRequest& request,
                                                EventSink& events) const
{
    emit(events, EventKind::info,
         "Loading package definition with " + std::string(name()));

    auto arguments = common_arguments(request);
    arguments.insert(arguments.begin(), "inspect");

    const auto process = detail::run_capture(helper_, arguments);
    if (process.exit_status != 0)
        throw Error(ErrorCode::invalid_definition,
                    "Pkgfile worker failed with status " +
                        std::to_string(process.exit_status));

    const auto fields = detail::split_nul(process.stdout_data);
    if (fields.size() != 8 || fields[0] != "pkgfile/0")
        throw Error(ErrorCode::invalid_definition,
                    "Pkgfile worker returned an unsupported definition record");

    PackageDefinition definition;
    definition.id = PackageId{fields[1], fields[2], fields[3]};
    for (const auto& source : split_sources(fields[4]))
        definition.sources.push_back(parse_source(source));
    definition.archive = ArchiveSpec{
        archive_format_from_string(fields[5]),
        compression_from_string(fields[6]),
    };
    definition.recipe = Recipe{
        RecipeFormat::pkgfile_v0,
        std::filesystem::absolute(request.paths.recipe_dir / "Pkgfile"),
        request.config_file ?
            std::optional<std::filesystem::path>(
                std::filesystem::absolute(*request.config_file)) :
            std::nullopt,
        fields[7],
    };

    return definition;
}

void PosixShellRecipeRunner::run(const RecipeRequest& request,
                                 EventSink& events) const
{
    if (request.definition.recipe.format != RecipeFormat::pkgfile_v0)
        throw Error(ErrorCode::recipe_failed,
                    "POSIX shell runner cannot execute this recipe format");

    emit(events, EventKind::info,
         "Running recipe with " + std::string(name()));

    std::vector<std::string> arguments = {
        "run",
        std::filesystem::absolute(request.paths.recipe_dir).string(),
        optional_path(request.definition.recipe.config_file),
        std::filesystem::absolute(request.paths.source_dir).string(),
        std::filesystem::absolute(request.paths.package_dir).string(),
        std::filesystem::absolute(request.paths.work_dir).string(),
        to_string(request.definition.archive.format),
        to_string(request.definition.archive.compression),
        std::filesystem::absolute(request.source_root).string(),
        std::filesystem::absolute(request.package_root).string(),
        request.definition.id.name,
        request.definition.id.version,
        request.definition.id.release,
    };

    const int status = detail::run_inherit(helper_, arguments);
    if (status != 0)
        throw Error(ErrorCode::recipe_failed,
                    "build recipe failed with status " +
                        std::to_string(status));
}

} // namespace pkgbuild
