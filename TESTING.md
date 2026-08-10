LIBPKGBUILD TESTING
===================

Qualification is separated by evidence role. Unit tests cover build-owned
identities, closed policy values, payload normalization, artifact authority,
and refusal of unsupported enum vocabulary. Integration tests compose the real
libpkgsource/libpkgcatalog/libpkgstate/libpkgresolve authority chain and exercise
exact direct input admission, unrestricted and constrained architecture binding,
request authority and identity, and complete success/failure result semantics.

The architecture integration test is a caller/callee contract test: libpkgsource
defines empty architecture sets as unrestricted, libpkgresolve admits such a
source for the selected architecture pair, and libpkgbuild preserves that
meaning rather than reinterpreting an empty set as an empty allow-list.

Every public header is compiled independently. The contract suite pins owner
authority, release metadata, generated pkg-config requirements, x86-64 public
value layout, exact ELF exports, direct source/resolver SONAME generations,
manual pages, test topology, and hosted-CI geometry. Shared builds must name
libpkgsource.so.3 and libpkgresolve.so.3 directly and must not retain obsolete
provider generations.

Hosted qualification constructs a clean pinned source/catalog/state/resolve
prefix before building libpkgbuild. GCC and Clang each exercise shared and
static debug builds; GCC also exercises a shared release build. Every product is
staged and consumed through its installed libpkgbuild.pc rather than through
source-tree include or link paths. Static consumption uses pkg-config --static.

GCC and Clang ASan+UBSan runs rebuild the same authority closure with sanitizers
enabled and execute the full project tests plus installed consumer. scdoc
generation, shell syntax, exact ABI checks, and git diff --check remain release
gates. ci/configure-and-test.sh implements the clean-prefix product gate used by
hosted CI; ci/qualify.sh remains a convenient local compiler/mode smoke matrix
for an already installed dependency closure.
