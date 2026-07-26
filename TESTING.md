libpkgbuild qualification
=========================

Core qualification
------------------

The existing corpus covers process isolation, source verification, source
revalidation, archive extraction, package staging, trusted normalization,
footprints, private workspaces, snapshot-bound definition execution, parity
comparison, and reference-tool behavior.

Artifact sealing adds direct checks that a successful snapshot-bound build:

. retains the exact source snapshot through BuildDefinition;
. reports one writer output without reconstructing its pathname;
. issues a SHA-256 SealedArtifactReceipt for that exact regular file;
. preserves the writer byte count; and
. removes a private workspace when retention was not requested.

Planner adapter qualification
-----------------------------

The optional adapter test builds a real package archive and inspects it through
the real libpkgimage libarchive backend.  It checks:

. exact archive bytes become the planner artifact identity;
. source release and candidate control remain bound to the build;
. the normalized image contains the expected payload paths;
. repeated projection has stable artifact-manifest identity;
. inconsistent build receipt fields are rejected; and
. archive mutation after build sealing is rejected by the exact-digest
  inspection precondition.

The adapter public header is also compiled independently.  Shared and static
qualification must verify that `libpkgbuild.pc` does not gain planner or image
dependencies while `libpkgbuild-plan.pc` publishes the complete adapter closure.

Compiler and release gates
--------------------------

Release qualification uses GCC and Clang with C++17, warnings enabled, and
warnings treated as errors.  It also runs shell syntax checks, whitespace
checks, public-header consumers, release metadata checks, and installed
pkg-config/linkage consumers where the build environment provides Meson.
