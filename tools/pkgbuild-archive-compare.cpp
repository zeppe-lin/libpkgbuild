#include "parity/archive_compare.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0]
                  << " REFERENCE-PACKAGE CANDIDATE-PACKAGE\n";
        return 2;
    }

    try {
        const auto report = pkgbuild::parity::compare_archives(argv[1], argv[2]);
        for (const auto& difference : report.differences)
            std::cout << pkgbuild::parity::format_difference(difference) << '\n';
        return report.equivalent() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "pkgbuild-archive-compare: " << error.what() << '\n';
        return 2;
    }
}
