LIBPKGBUILD 0.4.0
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
the pathname.  The same source is rehashed after consumption, so either
pathname replacement or mutation during use cannot introduce unverified
bytes into the recipe workspace.  Successful checks are retained in
`BuildReceipt::verifications`.

MD5 remains suitable only for legacy accidental-corruption detection;
it is not a malicious-content authentication mechanism.  Stronger
digest algorithms are available to non-Pkgfile definition backends.

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

After a root orchestrator receives the staged manifest, the workspace
root is returned to root ownership and mode 0700 before payload archive
creation.  This closes the build identity out of the handoff path.

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
* SourceExtractor and manifest-driven PackageWriter abstractions with
  a libarchive implementation.
* Virtual ownership, symlink, hardlink, FIFO, and device-node archive
  metadata.
* Private per-build workspaces.
* Structured BuildReceipt with exact artifact identity.
* Offline integration, execution-hardening, metadata, and archive
  integrity tests.

Not implemented yet
-------------------

* Footprint generation and checking.
* Binary stripping and manual-page compression.
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
