LIBPKGBUILD HISTORY
===================

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
* Renamed development-only enum spellings from package_root_v1/package_tar_v1
  to package_root/package_tar without inventing new semantic protocol versions.

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
