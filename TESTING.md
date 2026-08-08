LIBPKGBUILD TESTING
===================

Qualification is separated by evidence role. Unit tests cover build-owned
identities, closed policy values, payload normalization, and artifact authority.
Integration tests compose the real libpkgsource/libpkgcatalog/libpkgstate/
libpkgresolve boundary and exercise exact direct input admission, unrestricted
and constrained architecture binding, request authority and identity, and
complete success/failure result semantics.

The architecture integration test is a caller/callee contract test: libpkgsource
defines empty architecture sets as unrestricted, libpkgresolve admits such a
source for the selected architecture pair, and libpkgbuild must preserve that
meaning rather than reinterpret an empty set as an empty allow-list.

Every public header is compiled independently through the header suite. Shared
builds are checked against abi/libpkgbuild.exports. Contract tests pin authority,
release metadata, manual pages, test topology, and the reviewed ABI surface.

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
