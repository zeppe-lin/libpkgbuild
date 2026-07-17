LIBPKGBUILD PARITY HARNESS
==========================

The parity harness compares packages produced by the production shell
pkgmk and by libpkgbuild.  It is a migration gate, not a compatibility
shim: differences are reported and cause failure until they are either
fixed or deliberately accepted in the implementation.

Archive comparison
------------------

`pkgbuild-archive-compare` opens both package archives through
libpkgimage and compares their normalized installation semantics:

* package path presence;
* entry type;
* permission and special mode bits;
* numeric uid and gid;
* regular-file size;
* symbolic-link targets;
* hard-link group membership;
* device major and minor numbers; and
* SHA-256 hashes of regular-file payloads.

Archive order, compression representation, entry identifiers, and
modification timestamps are not compared.  Hard links are compared as
sorted path groups, so choosing a different regular member as the tar
hard-link target does not create a false difference.

Usage:

```
pkgbuild-archive-compare pkgmk.pkg.tar.gz libpkgbuild.pkg.tar.gz
```

Exit status is 0 for equivalent archives, 1 for semantic differences,
and 2 for invalid input or an inspection failure.

Corpus runner
-------------

`pkgbuild-parity` copies each immediate corpus directory containing a
Pkgfile into two isolated trees.  It runs pkgmk under fakeroot and runs
the reference libpkgbuild frontend with the same package definition,
source material, archive format, compression mode, and build identity.
The resulting archives are then compared directly.  The runner also
requires the exact package filename to match, so package name, version,
release, and compression identity cannot drift while contents remain equal.

A root caller must select an explicit non-root identity.  Example:

```
pkgbuild-parity \
    --pkgmk /usr/bin/pkgmk \
    --pkgbuild ./build/pkgbuild-example \
    --helper ./libpkgbuild/pkgbuild-pkgfile.in \
    --scanner ./build/pkgbuild-stage-scan \
    --fakeroot /usr/bin/fakeroot \
    --strip /usr/bin/strip \
    --build-user pkgbuild \
    --work-dir ./build/parity-work \
    ./tests/parity/corpus
```

For an ordinary caller, omit `--build-user`.  The runner exits 0 only
when every case is equivalent, 1 when at least one case differs, and 2
when a build or harness operation fails.  `--keep-work` retains both
build trees and prints their common workspace path.

The initial corpus covers ordinary files, numeric ownership, symbolic
and hard links, empty directories, manual-page compression, FIFOs, and
device nodes.  It is intentionally small enough for offline CI and
should be extended whenever a production package exposes another
semantic boundary.

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
