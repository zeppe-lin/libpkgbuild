LIBPKGBUILD 0.2.0
=================

libpkgbuild is the Zeppe-Lin package build engine.  It remains an
additive development project: the production shell pkgmk is not yet
replaced.

Execution model
---------------

Pkgfiles and legacy configuration are executable shell.  libpkgbuild
therefore treats process authority as an explicit input:

* A root caller must provide a non-root BuildIdentity.
* The definition worker and build recipe run with that uid, gid, and
  supplementary group set.
* Child processes receive an explicit environment, working directory,
  umask, and process group.
* Ambient process variables are not inherited by the POSIX executor.
* The Pkgfile backend rejects loader and shell hook variables such as
  LD_PRELOAD, BASH_ENV, ENV, CDPATH, and IFS.

The reference frontend accepts `--build-user USER`.  This is an
execution-safety boundary, not fakeroot metadata virtualization.
Package entries currently retain the real unprivileged staging owner;
a later staged-metadata backend must supply intended uid/gid values
before libpkgbuild can replace pkgmk in production.

Workspace model
---------------

`BuildPaths::work_dir` is a workspace base.  Each build creates a
private mode-0700 `.pkgbuild.XXXXXX` child and removes only that child.
The base may not be `/`, a symbolic link, or the same directory as the
recipe, source cache, or package output directory.  A retained
workspace is returned as `BuildReceipt::work_directory`.

Implemented
-----------

* DefinitionLoader abstraction.
* Pkgfile/0 definition backend using a private POSIX shell worker.
* Normalized PackageDefinition independent of Pkgfile syntax.
* Explicit ProcessExecutor and POSIX credential-drop backend.
* Downloader abstraction with a libcurl implementation.
* SourceExtractor and PackageWriter abstractions with a libarchive
  implementation.
* RecipeRunner abstraction with a Pkgfile/0 POSIX shell implementation.
* Private per-build workspaces.
* Structured BuildReceipt with exact artifact identity.
* Offline integration and execution-hardening tests.

Not implemented yet
-------------------

* Fakeroot or equivalent staged metadata capture.
* Checksums.
* Footprint generation and checking.
* Binary stripping and manual-page compression.
* Source mirrors.
* Up-to-date checks and force policy.
* Historical pkgmk CLI compatibility.
* recipe.yml/1 loader.
* Cancellation delivery to process groups.
* Hard-link preservation in package archives.

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
