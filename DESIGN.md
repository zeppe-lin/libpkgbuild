LIBPKGBUILD NATIVE AUTHORITY
============================

Authority boundary
------------------

libpkgsource owns package declarations, package-release identity, requirement
origins, selected profile identity, exact program bytes, source declarations,
and architecture constraints. libpkgbuild consumes one sealed source snapshot;
it never reopens or reparses its recipe document.

A resolver owns selection. A resolved package input binds one exact declared
build or check subject to a concrete package release, source snapshot, upstream
build result, and artifact. A materialized input additionally binds the exact
filesystem-tree identity made available to execution. These are separate facts:

    declared requirement
    resolved package input
    materialized package input

Source acquisition is likewise external. A materialized source is admitted only
when its observed SHA-256 equals the digest carried by the sealed source input.
Every declared source must appear exactly once in a build request.

Build request
-------------

A build request is sealed only after all source material, build inputs, check
inputs, architecture choices, profile facts, build program bytes, and policy are
complete. Requirement and source vectors are normalized where order is not
semantic. Program bytes and payload archive order remain exact.

The environment policy is closed and typed. Version 1 fixes C.UTF-8, UTC, an
isolated home, and denied network access, while sealing parallelism, umask, and
an optional SOURCE_DATE_EPOCH. There is no arbitrary inherited environment map.
An execution layer may realize this policy but may not add undeclared ambient
variables as build authority.

Build result
------------

A successful result requires all of:

* the exact sealed request;
* execution evidence identity;
* a complete ordered intended payload manifest;
* exact artifact byte count and SHA-256;
* artifact encoding; and
* an identity binding request, payload, and artifact.

A failed result contains no payload or artifact and requires separate failure
evidence. Partial result authority cannot be constructed through the public API.

The payload model retains canonical package paths, object type, mode, numeric
ownership, size, modification time, regular-content digest, symbolic-link
target, hard-link topology, and device numbers. Duplicate paths are rejected.
A hard link must target an earlier regular payload entry.

Artifact pathnames and filename conventions are excluded from identity. Exact
retained bytes are the artifact evidence. The core does not claim that archive
bytes represent the declared payload; that statement requires archive
inspection.

Planner adapter
---------------

libpkgbuild-plan accepts a successful build result plus a transport pathname.
It verifies the pathname byte count and exact SHA-256, opens the archive through
libpkgimage, compares every normalized image entry with the build payload, and
then projects:

* source-owned candidate control through libpkgsource-plan;
* exact artifact-byte identity; and
* an artifact-manifest identity binding the build result, candidate, payload,
  image, and inspection receipt.

Build/check inputs, source material, profile provenance, environment policy, and
execution evidence do not become planner candidate control.

Installed-state transition
--------------------------

libpkgstate 1.0.0 already defines build_provenance domains for build inputs,
build results, artifacts, and planner manifests. A later destination-owned
libpkgstate-build adapter should translate libpkgbuild values into those typed
references. libpkgbuild does not construct installed receipts and does not
invent filesystem application outcomes.

Non-goals for version 1
-----------------------

Version 1 intentionally contains no process executor, Linux namespaces,
Landlock, cgroups, source downloader, archive writer, transformation pipeline,
package installer, dependency resolver, collection scanner, Pkgfile parser, or
legacy importer.
