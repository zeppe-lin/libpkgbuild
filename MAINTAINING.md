LIBPKGBUILD MAINTAINING
======================

libpkgbuild owns pure package-build authority. It does not acquire source bytes,
execute programs, inspect archives, publish state, or project operation plans.
Changes that need those mechanisms belong in their provider or adapter boundary.

The 3.0.0 ABI is the first freeze of the opaque native build model and uses
SONAME libpkgbuild.so.4. The reviewed ELF surface contains an exact 219-symbol reviewed surface.
Private opaque-storage constructors, private value constructors, and private
identity constructors are not ABI. Public exceptions retain exported RTTI and
vtable authority so catches remain valid across independently built consumers.

libpkgsource and libpkgresolve are direct C++ ABI dependencies. Their accepted
major generations are explicit in meson.build and in generated pkg-config
metadata. Widening either upper bound requires a deliberate review of the C++
values consumed by libpkgbuild, even though resolver-bearing build values are
stored opaquely for downstream consumers.

A release candidate must pass GCC and Clang shared/static qualification, exact
ABI surface and layout contracts, provider DT_NEEDED checks, installed
pkg-config consumption, and GCC/Clang ASan+UBSan. Hosted CI constructs the
source/catalog/state/resolve authority closure from pinned revisions before it
builds this repository; it must not qualify against ambient stale libraries.
