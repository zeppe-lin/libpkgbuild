# libpkgbuild history

## 3.0.2

* Require callers to name every variable hermetic environment dimension:
  parallelism, file-creation mask, and optional source-date epoch. Hidden
  defaults are removed so controller omissions fail at compile time. Policy
  qualification mutates each dimension independently and requires both the
  environment identity and enclosing build-policy identity to change.

## 3.0.1

Authority-provider closure correction.

* Require `libpkgsource >= 4.0.0, < 5.0.0` and
  `libpkgresolve >= 4.0.0, < 5.0.0` together so build admission cannot mix a
  source-4 snapshot with a resolver carrying catalog-3 authority.
* Preserve `libpkgbuild.so.4`: public build carriers use opaque immutable
  storage and do not embed resolver or catalog C++ layouts by value.
* Re-pin installed-product qualification to the source-4/catalog-4/resolver-4
  authority closure.

## 3.0.0

Authority and ABI correction against the native source and resolver boundaries.

* Replaced caller-assembled resolved inputs with exact libpkgresolve requirement
  edges and selected-package authority.
* Removed false source-material and package-tree authorities. Source bytes remain
  libpkgfetch authority; concrete package resources remain execution authority.
* Derived source, release, profiles, build program, requirements, and selected
  architectures from the admitted catalog-backed resolver subject.
* Reduced the canonical build-request record to non-duplicated source, input,
  architecture, and policy authority.
* Made foreign-bearing input, request, and result values opaque so foreign C++
  layouts no longer propagate through downstream ABIs.
* Removed the in-tree libpkgbuild-plan adapter. Planner projection is a separate
  repository and archive/payload admission belongs to libpkgbuild-image.
* Added explicit symbol visibility, a reviewed ELF export manifest, standalone
  public-header tests, and bounded source/resolver dependency metadata.
* Bound the release candidate to libpkgsource 3.0.1 and libpkgresolve 3.x,
  rejected unsupported closed-vocabulary values at admission, removed private
  constructors from the reviewed ABI, and anchored public error RTTI.
* Added exact pkg-config, ABI-layout, provider-generation, installed-consumer,
  GCC/Clang shared/static, release, and ASan/UBSan qualification gates.
* Renamed development-only enum spellings from package_root_v1/package_tar_v1
  to package_root/package_tar without inventing new semantic protocol versions.
* Preserved libpkgsource unrestricted architecture semantics when build or target
  declaration sets are empty.
* Rejected intended hard-link payload entries whose shared-inode metadata
  contradicts their regular anchor.
* Standardized the Meson option file and root documentation layout on the
  current house conventions, and pinned those source-level conventions in the
  contract suite.
* Re-pinned hosted qualification to the exact released libpkgresolve 3.0.0
  authority head used by the local qualification closure.

Core ABI: libpkgbuild.so.4.

## 2.0.0

Source-authority ABI migration release.

* Rebuilt exact build requests and results against libpkgsource 2.0.0.
* Advanced the core ABI to libpkgbuild.so.3 because public request/result layouts
  embedded the source snapshot by value.
* Advanced the in-tree planner adapter ABI to libpkgbuild-plan.so.2.

## 1.0.0

First native package-build authority release. It replaced the historical
Pkgfile/pkgmk-shaped API with sealed requests and results, but still admitted
caller-constructed materialization and resolution claims and kept planner
projection in the core repository.

## 0.9.0

Last compatibility-shaped development release, retained only as history.
