LIBPKGBUILD 0.8.8
=================

libpkgbuild is the Zeppe-Lin package build engine.  It remains an
additive development project: the production shell pkgmk is not yet
replaced.

Execution model
---------------

Pkgfiles and legacy configuration are executable shell.  libpkgbuild
therefore treats process authority as an explicit input:

* A root caller must provide a non-root BuildIdentity.
* Definition workers and recipes run with that uid, gid, and
  supplementary group set.
* Child processes receive an explicit environment, working directory,
  umask, and process group.
* Ambient process variables are not inherited by the POSIX executor.
* Loader and shell hook variables such as LD_PRELOAD, BASH_ENV, ENV,
  CDPATH, and IFS are rejected at the Pkgfile boundary.
* Descendants left behind by a completed process are terminated with
  the process group.

The reference frontend accepts `--build-user USER`.  Recipe execution
never needs real root authority.

Source integrity model
----------------------

The private Pkgfile worker expands source words with the same shell field
splitting and pathname expansion used by legacy unquoted source iteration,
then returns a framed source list to the semantic backend.  The Pkgfile
backend parses `.md5sum` as data after inspection and binds every declared
source to exactly one typed digest.  Missing,
malformed, duplicate, ambiguous, or unrelated entries are rejected.
The backend does not silently create a missing checksum manifest; that
is an explicit maintenance operation rather than part of a build.

Cached and newly downloaded sources follow the same verification path.
The OpenSSL EVP verifier supports MD5, SHA-256, SHA-512, and BLAKE2b-512
through the normalized API.  Legacy Pkgfile definitions currently map
`.md5sum` entries to MD5 for compatibility with pkgmk.

A successful check returns a move-only VerifiedSource that owns the
file descriptor which was hashed.  Source copying and libarchive
extraction consume a duplicate of that descriptor rather than reopening
the pathname.  Libarchive extraction prefers UTF-8 archive path metadata
and falls back to native archive bytes when the active locale cannot
represent a valid UTF-8 name.  Recoverable pathname-conversion warnings
do not discard an otherwise valid entry.  Hard-link entries whose targets
appear later in the source archive are deferred and resolved after
ordinary payload extraction.  Unsafe and unresolved hard-link targets
are rejected.  The reference frontend
activates the locale selected by its explicit process environment before
loading definitions or extracting sources.  The same source is rehashed
after consumption, so either pathname replacement or mutation during use
cannot introduce unverified bytes into the recipe workspace.  Successful checks are retained in
`BuildReceipt::verifications`.

MD5 remains suitable only for legacy accidental-corruption detection;
it is not a malicious-content authentication mechanism.  Stronger
digest algorithms are available to non-Pkgfile definition backends.

Package normalization model
---------------------------

After recipe execution, libpkgbuild applies trusted transformations to
the staged package before the workspace is sealed and before archive
creation.  Transformations mutate both the payload tree and its
StagedPackage manifest and return structured receipts through
`BuildReceipt::transformations`.

The package-tree backend currently strips ELF executables with valid
section tables, ELF shared objects with valid section tables, and ar
archives.  Sectionless ELF firmware images remain opaque payloads.  A
hardlink group is transformed once through
a private copy, then every pathname is replaced and relinked to the
transformed inode.  Stripping therefore cannot silently split hardlinks.
For ordinary ar archives, member uid and gid header fields are restored from
the pre-strip archive after validating that strip preserved the member
sequence.  Trusted normalization therefore cannot leak the real build
identity into static-library payloads when it runs outside fakeroot.
Legacy `.nostrip` files are normalized as POSIX basic regular expressions;
if any pathname in a hardlink group is excluded, the entire group is
left unchanged.

Manual pages beneath `*/man/man*/*` are compressed with deterministic
gzip headers.  Hardlinked manual pages are compressed once and recreated
as a hardlink group with `.gz` names.  Relative and absolute manual-page
symlinks are renamed and retargeted only when their referenced payload
was actually compressed.  When a recipe already installed the equivalent
compressed symlink, the redundant uncompressed alias is removed and both
representations are coalesced; incompatible collisions still fail.  A
hardlink group spanning manual-page and non-manual locations is preserved
unchanged rather than having its identity fractured.

Footprint model
---------------

After trusted package-tree normalization, libpkgbuild derives a
normalized Footprint directly from StagedPackage.  Footprint entries
retain canonical package paths, object types, symbolic modes, numeric
ownership, and symbolic-link targets.  Comparison returns structured
added, removed, and changed entries rather than parsing textual diff
output.

The legacy `.footprint` codec resolves owner and group names to numeric
identities, validates directory suffixes and symbolic-link records, and
rejects malformed or duplicate package paths.  Serialization is sorted
and deterministic, and replacement uses a temporary file beside the
manifest followed by an atomic rename.

BuildRequest exposes explicit ignore, compare, and write policies.
Ordinary builds still ignore footprints by default.  A comparison
mismatch throws FootprintMismatch carrying the complete structured
difference before archive creation; a write policy must be requested
explicitly and is recorded in BuildReceipt::footprint.

Differential parity model
-------------------------

The optional parity tools use libpkgimage 0.2.1 or later without adding
libpkgimage to the libpkgbuild runtime dependency closure.
`pkgbuild-archive-compare` compares normalized archive semantics and
SHA-256 payload hashes while ignoring archive order, timestamps, and the
choice of hard-link target member.  Gzip-compressed manual pages are
compared through their decompressed content, so legacy gzip filename and
timestamp headers do not obscure equivalent installation payloads.
Ordinary static ar archives are recognized by `!<arch>\n` payload magic
and compared after removing only member timestamp fields; a linker script
or other ordinary file named `.a` remains byte-sensitive.  Member order,
names, modes, uid/gid fields, long-name and symbol tables, and member
payloads remain significant.  `pkgbuild-parity` builds
Pkgfile cases in isolated trees with both production pkgmk and the
reference libpkgbuild frontend.  It accepts the bundled directory
corpus or an ordered manifest of real package directories.  Corpus
directory basenames are preserved and must match the Pkgfile package
name.  Shell-expanded recipe-local and sibling local sources are staged
with their collection-relative topology while collection escapes are
rejected.
Both builders share one canonical recipe, configuration path, source
cache, package-output directory, absolute
private workspace path, private temporary directory, and explicit
`C.UTF-8` locale.  The candidate allocates the workspace through the
production engine; the runner then moves its archive aside, resets the
workspace, and lets pkgmk reuse the exact path.  The common `TMPDIR` is a
protected sibling rather than a child of `PKGMK_WORK_DIR`, because pkgmk
removes its work directory during startup.

An initial mismatch triggers stability checks at that same absolute
workspace path.  The candidate is rebuilt first.  If its two semantic
packages differ, the case is classified as `NONDETERMINISTIC_OUTPUT` for
libpkgbuild and no second pkgmk build is required.  Otherwise pkgmk is
rebuilt at the same path and receives the same classification if its two
packages differ.  A mismatch is reported as `SEMANTIC_MISMATCH` only when
both engines are internally stable.

A campaign may source the same baseline pkgmk configuration and may
download missing sources explicitly.  Pkgfile inspection, recipe staging,
and local-source preparation failures are retained per case instead of
aborting the manifest.  Legacy build failures, candidate build failures,
post-build artifact inspection failures,
nondeterministic outputs, and semantic mismatches are classified
separately and do not stop later cases.  Per-run packages, combined
stdout/stderr logs, exact workspace records, and cross/repeat
comparison reports are retained beneath the private run directory;
successful case trees are discarded unless `--keep-work` was requested.
Every completed campaign retains a bounded human `report.txt` and a
machine-readable `results.tsv`.  Compact terminal output shows only indexed
case progress, while `--verbose-builds` restores complete log relay.  Report
detail and failed-log tail limits are configurable, and `--report` copies the
human report to a stable external path for remote qualification workflows.

The runner executes pkgmk under fakeroot and applies the same explicit
non-root build identity used by the candidate build.  It never mutates
the corpus and does not provide semantic allow-lists.  The bundled
corpus covers regular metadata, numeric ownership, symbolic and hard
links, manual-page normalization, FIFOs, and device nodes.  Real package
campaigns remain the release gate before pkgman integration.  See
`PARITY.md` for manifest syntax, command examples, evidence layout, and
comparison rules.

Staged metadata model
---------------------

The fakeroot Pkgfile runner stores fakeroot state, scans the package
root inside that state, and returns a normalized StagedPackage.  The
manifest records canonical paths, object types, modes, uid/gid values,
modification times, symbolic-link targets, hardlink relationships, and
device numbers.

The package writer treats that manifest as authoritative metadata.  It
reads regular-file payloads beneath an open package-root descriptor,
without following symbolic links, and rejects payloads changed after
metadata capture.  Virtual root ownership and special files therefore
survive into the package archive while the recipe still runs as an
ordinary user.

Workspace model
---------------

`BuildPaths::work_dir` is a workspace base.  Each ordinary build creates
a private mode-0700 `.pkgbuild.XXXXXX` child and removes only that child.
A trusted orchestrator may set `BuildRequest::workspace_directory` to an
absent absolute `.pkgbuild.*` direct child of the same base when an exact
path must be replayed.  Existing paths, relative paths, other names, and
paths outside the base are rejected.  The reference frontend exposes
this contract as `--workspace-dir DIR`.

The base may not be `/`, a symbolic link, or the same directory as the
recipe, source cache, or package output directory.  A retained workspace
is returned as `BuildReceipt::work_directory`.

After a root orchestrator receives the staged manifest and trusted
normalization completes, the workspace root is returned to root ownership
and mode 0700 before payload archive creation.  This closes the build
identity out of the handoff path.

Implemented
-----------

* DefinitionLoader abstraction.
* Pkgfile/0 definition backend using a private POSIX shell worker.
* Normalized PackageDefinition independent of Pkgfile syntax.
* Explicit ProcessExecutor and POSIX credential-drop backend.
* Fakeroot-backed staged metadata capture.
* Normalized StagedPackage model and manifest protocol.
* Downloader abstraction with a libcurl implementation.
* Typed source digests and descriptor-stable OpenSSL verification.
* Strict legacy `.md5sum` normalization for Pkgfile definitions.
* Structured package-tree transformation contracts and receipts.
* Hardlink-safe ELF, shared-object, and ar archive stripping with
  sectionless ELF firmware exclusion.
* Virtual ar member ownership preserved across trusted stripping.
* POSIX-BRE `.nostrip` normalization for Pkgfile definitions.
* Deterministic, hardlink-safe manual-page compression and symlink
  coalescing.
* Normalized footprint generation, legacy manifest parsing, comparison,
  and atomic replacement.
* libpkgimage-based semantic archive comparison, including magic-based
  ar detection and timestamp normalization, and pkgmk differential corpus
  runner.
* Shell-compatible Pkgfile source-word expansion and framed helper
  records.
* Ordered real-package manifests with isolated shared source caches and
  collection-relative sibling local sources.
* Per-case preparation, build, artifact-inspection, instability, and
  semantic failure classification with retained diagnostic evidence.
* Compact parity progress, bounded human campaign reports, and deterministic
  per-case TSV result ledgers.
* Same-path sequential pkgmk/libpkgbuild parity execution with a shared
  private TMPDIR.
* On-demand same-path repeat builds and nondeterministic-output
  classification.
* Controlled exact private workspace replay for trusted orchestrators.
* SourceExtractor and manifest-driven PackageWriter abstractions with
  a libarchive implementation.
* Forward-hardlink-safe, locale-independent UTF-8 source extraction.
* Virtual ownership, symlink, hardlink, FIFO, and device-node archive
  metadata.
* Private per-build workspaces.
* Structured BuildReceipt with exact artifact identity.
* Offline integration, execution-hardening, metadata, archive-integrity,
  transformation rollback, and normalization pipeline tests.

Not implemented yet
-------------------

* Source mirrors.
* Up-to-date checks and force policy.
* Historical pkgmk CLI compatibility.
* recipe.yml/1 loader.
* External cancellation delivery to active process groups.

Build and test
--------------

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
```

The offline vertical slice can also be compiled directly:

```sh
./tests/run-example.sh
```

Sealed artifact authority
-------------------------

The snapshot-bound engine does not treat a successful package-writer return or
an archive pathname as artifact identity.  After publication, it opens the
reported regular file without following a final symbolic link, verifies the
writer pathname and byte count, hashes the exact retained bytes with SHA-256,
and rejects mutation during sealing.  Successful snapshot-bound builds retain
that evidence as `BuildReceipt::artifact`.

The writer-facing `ArchiveReceipt` remains operational evidence about archive
creation.  `SealedArtifactReceipt` is the build authority over the exact
published bytes.  The separation prevents a backend success code or filename
convention from becoming package identity by accident.

Planner artifact adapter
------------------------

The optional `libpkgbuild-plan` library composes the sealed build result with
`libpkgsource-plan`, `libpkgimage`, and `libpkgplan`.  It reopens the reported
archive through a caller-supplied libpkgimage backend while requiring the exact
build-engine SHA-256 digest, then returns a lifetime-bound projection retaining:

* the complete BuildReceipt;
* the source-issued candidate projection;
* the exact inspected package image and inspection receipt; and
* the planner artifact and artifact-manifest facts.

The planner artifact identity is the exact archive-byte digest in the planner
artifact domain.  The artifact-manifest identity binds the source snapshot,
candidate release and control, exact archive bytes, normalized package image,
and inspection receipt.  Archive filenames remain labels only.

Build with the adapter explicitly enabled using:

```
meson setup build -Dplanner_adapter=enabled
```

Disabling the adapter leaves libpkgplan and libpkgimage outside the core
libpkgbuild dependency closure.
