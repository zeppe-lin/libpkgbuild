LIBPKGBUILD 0.8.2
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

The Pkgfile backend parses `.md5sum` as data after Pkgfile inspection
and binds every declared source to exactly one typed digest.  Missing,
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
the pathname.  Libarchive extraction defers hard-link entries whose targets
appear later in the source archive and resolves valid chains after ordinary
payload extraction.  Unsafe and unresolved hard-link targets are rejected.
The same source is rehashed after consumption, so either pathname replacement
or mutation during use cannot introduce unverified bytes into the recipe
workspace.  Successful checks are retained in
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

The package-tree backend currently strips ELF executables, ELF shared
objects, and ar archives.  A hardlink group is transformed once through
a private copy, then every pathname is replaced and relinked to the
transformed inode.  Stripping therefore cannot silently split hardlinks.
Legacy `.nostrip` files are normalized as POSIX basic regular expressions;
if any pathname in a hardlink group is excluded, the entire group is
left unchanged.

Manual pages beneath `*/man/man*/*` are compressed with deterministic
gzip headers.  Hardlinked manual pages are compressed once and recreated
as a hardlink group with `.gz` names.  Relative and absolute manual-page
symlinks are renamed and retargeted only when their referenced payload
was actually compressed.  A hardlink group spanning manual-page and
non-manual locations is preserved unchanged rather than having its
identity fractured.

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
`pkgbuild-parity` builds Pkgfile cases in isolated trees with both
production pkgmk and the reference libpkgbuild frontend.  It accepts the
bundled directory corpus or an ordered manifest of real package
directories.  Corpus directory basenames are preserved and must match
the Pkgfile package name.  Both builders share one canonical recipe,
configuration path, source cache, package-output directory, absolute private
workspace path, and private temporary directory.  The candidate allocates the
workspace through the production engine; the runner then moves its archive
aside, resets the workspace, and lets pkgmk reuse the exact path.  The common
`TMPDIR` is a protected sibling rather than a child of `PKGMK_WORK_DIR`, because
pkgmk removes its work directory during startup.

A campaign may source the same baseline pkgmk configuration and may
download missing sources explicitly.  Legacy build failures, candidate
build failures, and semantic mismatches are classified separately and
do not stop later cases.  Failed package trees, combined stdout/stderr
build logs, candidate and legacy archives, the final legacy workspace,
and structured comparison reports are retained beneath the private run
directory; successful trees are discarded unless `--keep-work` was
requested.

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

`BuildPaths::work_dir` is a workspace base.  Each build creates a
private mode-0700 `.pkgbuild.XXXXXX` child and removes only that child.
The base may not be `/`, a symbolic link, or the same directory as the
recipe, source cache, or package output directory.  A retained
workspace is returned as `BuildReceipt::work_directory`.

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
* Hardlink-safe ELF, shared-object, and ar archive stripping.
* POSIX-BRE `.nostrip` normalization for Pkgfile definitions.
* Deterministic, hardlink-safe manual-page compression and symlink rewrite.
* Normalized footprint generation, legacy manifest parsing, comparison, and atomic replacement.
* libpkgimage-based semantic archive comparison and pkgmk differential corpus runner.
* Ordered real-package manifests with isolated shared source caches.
* Per-case parity failure classification and retained diagnostic evidence.
* Same-path sequential pkgmk/libpkgbuild parity execution with a shared private TMPDIR.
* SourceExtractor and manifest-driven PackageWriter abstractions with
  a libarchive implementation.
* Forward-hardlink-safe source extraction.
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
