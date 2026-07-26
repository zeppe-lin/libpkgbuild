libpkgbuild authority model
===========================

Purpose
-------

libpkgbuild realizes one immutable package-source snapshot under one explicit
build policy.  It owns build execution, trusted package-tree normalization,
archive production, and evidence that identifies the exact published archive
bytes.  It does not own source interpretation, package archive semantics,
package planning, target state, or installation effects.

Authority graph
---------------

The source-to-build path is:

```
libpkgsource source_snapshot
        |
        v
libpkgbuild BuildDefinition
        |
        v
libpkgbuild BuildReceipt
        |
        +-- ArchiveReceipt          writer operation evidence
        `-- SealedArtifactReceipt   exact published-byte evidence
```

A successful BuildReceipt retains the BuildDefinition that issued the build,
the exact source snapshot through that definition, the package writer receipt,
and a build-engine seal over the exact archive bytes.  The archive pathname is
a transport label.  It is not an artifact identity.

The optional planning composition is:

```
BuildReceipt
+ libpkgsource-plan candidate projection
+ libpkgimage exact-byte inspection
-----------------------------------------
libpkgbuild-plan artifact_projection
```

libpkgbuild-plan owns only the translation from completed build evidence into
planner artifact vocabulary.  It does not plan a package operation and does not
open source collections independently.

Exact artifact seal
-------------------

The package writer reports where it published an archive, the byte count it
observed, and the selected archive encoding.  After the writer returns, the
engine opens that output without following a final symbolic link, hashes the
opened regular file with SHA-256, and verifies that size and stable file metadata
did not change during hashing.  The resulting SealedArtifactReceipt records:

* the exact absolute output pathname used as a label;
* the exact byte count observed through the retained descriptor; and
* the SHA-256 digest of those bytes.

The engine rejects a writer receipt whose pathname, byte count, object type, or
published object does not match the requested output.  Package planning never
accepts writer success alone as artifact authority.

Planner adapter
---------------

libpkgbuild-plan accepts one successful BuildReceipt and one archive backend. It:

1. projects the retained source snapshot through libpkgsource-plan;
2. verifies package coordinates and snapshot provenance;
3. asks libpkgimage to inspect the sealed artifact pathname while requiring the
   exact build-engine digest;
4. issues the planner artifact identity from the exact archive digest;
5. issues an artifact-manifest identity over source, candidate, archive, image,
   and inspection authority; and
6. returns a lifetime-bound value retaining all of those inputs.

The adapter rejects missing or malformed artifact seals, path or byte-count
mismatches, source/candidate mismatches, changed archive bytes, image/receipt
mismatches, and planner vocabulary rejection.

Non-authorities
---------------

libpkgbuild and libpkgbuild-plan do not:

* derive a package artifact from its filename;
* claim that an archive pathname is immutable;
* replace libpkgimage archive inspection;
* derive target observations or ownership;
* select installation, upgrade, or removal policy;
* construct an installed package record; or
* apply package effects.

Dependency direction
--------------------

The core library depends on libpkgsource and its existing execution
prerequisites.  It does not depend on libpkgplan or libpkgimage.

The optional libpkgbuild-plan adapter depends on:

```
libpkgbuild
libpkgsource-plan
libpkgimage
libpkgplan
```

Disabling the adapter leaves the core public headers, pkg-config closure, and
shared-library dependency graph unchanged apart from the intentional
SealedArtifactReceipt ABI introduced by this release.
