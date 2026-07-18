LIBPKGBUILD PARITY HARNESS
==========================

The parity harness compares packages produced by the production shell
pkgmk and by libpkgbuild.  It is a migration gate, not a compatibility
shim: differences are reported and cause failure until they are either
fixed or deliberately accepted in both implementations.  There is no
semantic allow-list.

Archive comparison
------------------

`pkgbuild-archive-compare` opens both package archives through
libpkgimage and compares their normalized installation semantics:

* package path presence;
* entry type;
* permission and special mode bits;
* numeric uid and gid;
* regular-file semantic size;
* symbolic-link targets;
* hard-link group membership;
* device major and minor numbers; and
* SHA-256 hashes of regular-file semantic payloads.

For gzip-compressed manual pages beneath `*/man/man*/*.gz`, both inputs
must be valid single-member gzip streams.  Their decompressed sizes and
payload hashes are compared, so gzip filename and timestamp headers do
not create false differences.  Other gzip files remain byte-sensitive.

Regular files whose package path ends in `.a` must be valid ordinary ar
archives.  Their member timestamp fields are removed from the semantic
hash, because sequential builds necessarily stamp them at different
wall-clock times.  Every other archive byte remains significant, including
member order, names, modes, uid/gid fields, long-name and symbol-table
payloads, object
payloads, and padding.  Thin archives remain invalid package payloads.

Archive order, package compression representation, entry identifiers,
and modification timestamps are not compared.  Hard links are compared
as sorted path groups, so choosing a different regular member as the tar
hard-link target does not create a false difference.

Usage:

```
pkgbuild-archive-compare pkgmk.pkg.tar.gz libpkgbuild.pkg.tar.gz
```

Exit status is 0 for equivalent archives, 1 for semantic differences,
and 2 for invalid input or an inspection failure.

Corpus runner
-------------

`pkgbuild-parity` copies every package into one canonical recipe
directory without changing its basename.  The basename must match the
Pkgfile `name`, as required by both builders.  The exact package filename
must also match, covering package name, version, release, and compression
identity.

The candidate runs first and allocates its normal private
`.pkgbuild.XXXXXX` workspace.  Its archive is moved aside, the workspace
is reset at that exact path, and pkgmk is then configured to reuse it.
Both builders therefore use the same recipe directory, configuration
path, source cache, package-output directory, `$SRC`, `$PKG`, and
`TMPDIR`.

The runner sets `LANG=C.UTF-8` and `LC_ALL=C.UTF-8` for both engines,
matching the production pkgmk/bsdtar build contract instead of inheriting
an arbitrary caller locale.  The common temporary directory is a private
sibling of the reused workspace, not a child of it: pkgmk removes
`PKGMK_WORK_DIR` before starting a build.  This prevents both deleted
temporary-directory failures and paths embedded by compilers, libtool,
LTO, or generated files from creating false semantic mismatches.

The bundled synthetic corpus can be passed as a directory:

```
pkgbuild-parity [options] ./tests/parity/corpus
```

A real campaign uses `--manifest FILE`.  Each non-empty line is one
package directory.  Lines whose first non-whitespace character is `#`
are comments.  Relative paths are resolved against the manifest's
parent directory.  Manifest order is preserved.  Duplicate resolved
paths and duplicate package-directory basenames are rejected.

For example:

```
# parity-corpus.list
../pkgsrc-core/bash
../pkgsrc-core/coreutils
../pkgsrc-system/openssl
../pkgsrc-system/zlib
../pkgsrc-xorg/xorg-server
```

Run the real campaign with the production build configuration and
explicit source acquisition:

```
pkgbuild-parity \
    --pkgmk /usr/sbin/pkgmk \
    --pkgbuild "$PWD/build/pkgbuild-example" \
    --helper "$PWD/libpkgbuild/pkgbuild-pkgfile.in" \
    --scanner "$PWD/build/pkgbuild-stage-scan" \
    --fakeroot /usr/bin/fakeroot \
    --strip /usr/bin/strip \
    --build-user pkgbuild \
    --config /etc/pkgmk.conf \
    --download \
    --manifest "$PWD/parity-corpus.list" \
    --work-dir "$PWD/build/parity-work"
```

A root caller must select an explicit non-root identity.  An ordinary
caller omits `--build-user`.

Results and evidence
--------------------

Every package produces exactly one result line:

```
PASS package
LEGACY_BUILD_FAILED package
CANDIDATE_BUILD_FAILED package
NONDETERMINISTIC_OUTPUT package
SEMANTIC_MISMATCH package
```

A package-level failure does not stop the remaining manifest.  The final
summary reports counts for each class.  Exit status is 0 only when every
case passes, 1 when any package fails or differs, and 2 for invalid
arguments or a harness-level operation that prevents the campaign from
continuing.

A cross-engine mismatch is not immediately accepted as semantic.  The
runner resets the original private workspace and rebuilds the candidate
at that exact path.  If candidate run 1 and run 2 differ semantically,
the result is `NONDETERMINISTIC_OUTPUT` with `engine: libpkgbuild`.
Otherwise the runner resets the same path again and repeats pkgmk.  A
legacy repeat difference receives `engine: pkgmk`.  Only two stable
engines can produce `SEMANTIC_MISMATCH`.  Repeat builds are therefore
paid only by cases that already differ.

Both builders run with work retention enabled.  On failure, the case is
moved beneath the private run workspace:

```
<work-base>/.pkgbuild-parity.XXXXXX/failed/<package>/
    recipe/<package>/
    pkgmk/
        run-1/{packages/,build.log,workspace.txt}
        run-2/{packages/,build.log,workspace.txt}  # when reached
        repeat-comparison.txt                     # when reached
    libpkgbuild/
        run-1/{packages/,build.log,workspace.txt}
        run-2/{packages/,build.log,workspace.txt}  # on mismatch
        repeat-comparison.txt                      # on mismatch
    packages/
    sources/
    work/.pkgbuild.XXXXXX/
    tmp/
    pkgmk.conf
    cross-comparison.txt
    comparison.txt
```

A successful candidate workspace is deliberately reset before each later
builder so all reached runs occupy the same absolute path.  Each run keeps
its archive, complete combined build log, and selected workspace pathname.
The final `work/.pkgbuild.XXXXXX` tree belongs to the last reached run.

`cross-comparison.txt` records the initial engine difference.  Per-engine
`repeat-comparison.txt` files record either `equivalent` or the repeat
differences.  `comparison.txt` records the source package directory,
result class, and final structured diagnostics.  `FAILED_WORK` prints the
retained failure root.
Successful case trees are removed unless `--keep-work` is supplied; with
that option, `WORK` also prints the complete run workspace.

Each `build.log` contains merged standard output and standard error in one
stream, including recipe tracing, compiler diagnostics, and frontend errors.
The same stream is also relayed to the invoking terminal while the campaign
runs.

Initial corpus
--------------

The bundled corpus covers ordinary files, numeric ownership, symbolic
and hard links, empty directories, manual-page compression, FIFOs, and
device nodes.  `tests/parity/corpus.manifest` is the equivalent manifest
form used by the runner tests.

A representative real corpus should cover at least:

* plain make, Autotools, CMake, and Meson recipes;
* multiple source archives, renamed URI sources, and local patches;
* generated files and architecture-dependent output;
* PIE executables, shared libraries, and static ar archives;
* `.nostrip` exclusions and hardlinked binaries;
* ordinary, symlinked, and hardlinked manual pages;
* non-root package ownership, special files, and empty directories;
* 32-bit mode where supported; and
* every supported package compression mode.

Pkgman integration gate
-----------------------

The production build path may switch to libpkgbuild only after all of
the following are true:

```
[ ] bundled synthetic corpus passes
[ ] representative real-package manifest passes
[ ] the real manifest passes on repeated clean runs
[ ] source, ELF, .nostrip, link, ownership, and special-file cases pass
[ ] the hardlinked-manpage policy is identical in both builders
[ ] every observed difference is fixed or adopted in both builders
[ ] no unexplained retained failure remains
```

After the gate is green, pkgman may consume `BuildReceipt::package` and
continue to invoke external pkgadd temporarily.  Package planning and
application remain separate later migrations.

Building
--------

The tools require libpkgimage 0.2.1 or later.  The Meson feature option
is `auto` by default:

```
meson setup build -Dparity=enabled
meson compile -C build
meson test -C build --print-errorlogs
```

Use `-Dparity=disabled` for a library-only build without libpkgimage.
