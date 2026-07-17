LIBPKGBUILD 0.3.0
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

* Checksums.
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
