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

Meson topology qualification
----------------------------

The topology contract checks that:

. pkg-config support is imported before adapter metadata is generated;
. the configured `pkgbuild-pkgfile` worker has one build-tree path and one
  installed libexec path;
. the stage scanner has one executable target and explicit build/install paths;
. core tests receive the project-owned worker and scanner targets;
. planner-adapter tests obtain `pkgsource-pkgfile-worker` from libpkgsource
  dependency metadata;
. parity and planner feature gates remain independent; and
. no test reaches into a source template or guesses a sibling build path.

The forge-neutral qualification matrix builds shared and static variants with
both optional surfaces enabled, runs all tests, installs through `DESTDIR`, and
checks that both project-owned libexec programs are present and executable.  It
then configures core-only, planner-only, and parity-only profiles to prove that
an explicit feature disable remains authoritative.

Compiler and release gates
--------------------------

Release qualification uses GCC and Clang with C++17, warnings enabled, and
warnings treated as errors.  It also runs shell syntax checks, whitespace
checks, public-header consumers, release metadata checks, and installed
pkg-config/linkage consumers where the build environment provides Meson.
