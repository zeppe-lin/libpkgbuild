LIBPKGBUILD 1.0.0
=================

libpkgbuild defines the native Zeppe-Lin package-build authority model.
Version 1 is intentionally incompatible with the former Pkgfile/pkgmk-shaped
execution engine.

The core library seals:

* source material verified against one libpkgsource snapshot;
* exact build and check package inputs selected by an external resolver;
* content-addressed trees materialized for those inputs;
* selected build and target architectures;
* source-owned selected profiles and exact build program bytes;
* a closed hermetic process-environment policy;
* one immutable build request;
* one complete intended package payload;
* exact retained artifact bytes; and
* successful or failed build-result authority.

The library does not parse recipe syntax, inspect collections, resolve
requirements, download or extract sources, execute programs, create sandboxes,
write archives, install packages, or query installed state. Pathnames are
transport coordinates and never artifact identity.

The optional libpkgbuild-plan adapter inspects exact artifact bytes through
libpkgimage, proves that the archive image equals the build-owned payload, and
projects only the verified artifact facts accepted by libpkgplan.

No Pkgfile/0, pkgmk behavior, fakeroot, `.md5sum`, `.footprint`, `.nostrip`,
`.32bit`, or mutable build-directory compatibility exists in version 1.
Migration belongs to separate tools and execution backends.

See DESIGN.md for authority boundaries and TESTING.md for qualification.
