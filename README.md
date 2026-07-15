LIBPKGBUILD WORKING EXAMPLE
===========================

This directory is a vertical slice for the proposed libpkgbuild
package build engine.
It is intentionally additive: the current shell pkgmk remains
untouched.

Implemented
-----------

* DefinitionLoader abstraction.
* Pkgfile/0 definition backend using a private POSIX shell worker.
* Normalized PackageDefinition independent of Pkgfile syntax.
* Downloader abstraction with a libcurl implementation.
* SourceExtractor and PackageWriter abstractions with a libarchive
  implementation.
* RecipeRunner abstraction with a Pkgfile/0 POSIX shell
  implementation.
* Engine orchestration that returns a structured BuildReceipt.
* Tiny pkgbuild-example frontend.
* Offline integration fixture that produces a real package archive.

Not implemented yet
-------------------

* Checksums.
* Footprint generation and checking.
* Binary stripping and manual-page compression.
* Source mirrors.
* Up-to-date checks and force policy.
* Historical pkgmk CLI compatibility.
* recipe.yml/1 loader.
* Cancellation and process-group control.
* Hard-link preservation in package archives.

Build with Meson
----------------

```sh
meson setup build
meson compile -C build
```

Run the offline vertical-slice test
-----------------------------------

```sh
./tests/run-example.sh
```

The test compiles the example directly when Meson is unavailable,
loads tests/fixtures/hello/Pkgfile, executes build(), and creates:

```text
.example-build/packages/hello#1.0-1.pkg.tar.gz
```
