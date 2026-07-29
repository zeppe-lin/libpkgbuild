LIBPKGBUILD HISTORY
===================

## 2.0.0

Source-authority ABI migration release.

* Rebuilt exact build requests and results against `libpkgsource 2.0.0`.
* Preserved the complete source snapshot, including optional exact check
  program authority, without interpreting or executing that program.
* Advanced the core ABI to `libpkgbuild.so.3`: `build_request` and
  `build_result` embed the enlarged source snapshot by value.
* Advanced the planner adapter ABI to `libpkgbuild-plan.so.2` because
  `artifact_projection` embeds a complete build result.
* Raised dependency floors to `libpkgsource >= 2.0.0` and
  `libpkgsource-plan >= 2.0.0`.
* Preserved build/result identity domains and artifact admission semantics.

Core ABI: libpkgbuild.so.3.
Planner adapter ABI: libpkgbuild-plan.so.2.

## 1.0.0

First native Zeppe-Lin package-build authority release.

* Replaced the Pkgfile/pkgmk-shaped public API with sealed native requests and
  results.
* Bound verified source material directly to libpkgsource 1.0.0 declarations.
* Distinguished declared requirements, resolved package inputs, and
  materialized package trees.
* Added exact build/check scope validation, architecture selection, profile
  retention, and a closed hermetic environment policy.
* Added complete intended payload manifests, exact artifact-byte authority,
  success/failure evidence, and domain-separated identities.
* Rebuilt libpkgbuild-plan around exact archive inspection and payload equality.
* Removed Pkgfile workers, fakeroot, MD5, pkgmk parity, sidecar files, mutable
  workspaces, source acquisition, execution, transformation, and archive-writing
  responsibilities from the authoritative library.

Core ABI: libpkgbuild.so.2.
Planner adapter ABI: libpkgbuild-plan.so.1.

There is no in-library upgrade path from the 0.x API. Native callers must
construct sealed source, resolver, materialization, execution, and artifact
evidence. Historical compatibility belongs to separate migration tools.

## 0.9.0

Last compatibility-shaped development release. It is retained only as project
history and is not a native authority predecessor that version 1 can import.
