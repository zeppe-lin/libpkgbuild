LIBPKGBUILD TESTING
===================

Behavior tests exercise exact resolver admission, profile-expanded direct
build/check inputs, architecture binding, closed environment policy, payload
normalization, artifact authority, deterministic request sealing, complete
success/failure results, and opaque public request layout.

Every public header is compiled independently and through the umbrella header.
Shared builds are checked against abi/libpkgbuild.exports. Repository contracts
reject the removed materialization/tree types, in-tree planner code, unbounded
foreign ABI dependencies, obsolete protocol numbering, and stale documentation.

Release qualification runs separate shared and static builds:

    meson setup build-shared . \
        -Ddefault_library=shared \
        -Dlink_mode=shared \
        -Dman_pages=enabled \
        -Dwerror=true
    meson compile -C build-shared
    meson test -C build-shared --print-errorlogs

    meson setup build-static . \
        -Ddefault_library=static \
        -Dlink_mode=static \
        -Dman_pages=enabled \
        -Dwerror=true
    meson compile -C build-static
    meson test -C build-static --print-errorlogs

GCC and Clang, ASan/UBSan, installed shared/static consumers, pkg-config
metadata, DT_NEEDED inspection, scdoc generation, shell syntax, and
git diff --check are release gates when their tools are available.
