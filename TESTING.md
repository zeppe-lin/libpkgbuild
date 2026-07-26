LIBPKGBUILD TESTING
===================

The model tests cover identity validation, verified source binding, exact package
subjects, closed environment policy, package-path normalization, duplicate and
hard-link rejection, deterministic request sealing, build/check separation,
architecture admission, result completeness, and result identity changes.

The planner adapter test writes exact package archive bytes, seals a matching
successful result, reopens the artifact through the exact libpkgimage backend,
checks payload equality, and confirms the projected libpkgplan facts. A payload
metadata mismatch is rejected.

Release qualification should run:

    meson setup build . \
        -Dplanner_adapter=enabled \
        -Dman_pages=enabled \
        -Dwerror=true
    meson compile -C build
    meson test -C build --print-errorlogs

Shared and static builds must be configured separately with matching
`default_library` and `link_mode` values. GCC and Clang are both release gates.
ASan and UBSan runs, standalone installed-header consumers, pkg-config consumers,
SONAME inspection, `git diff --check`, shell syntax, scdoc generation, and mandoc
lint should be included when those tools are available.
