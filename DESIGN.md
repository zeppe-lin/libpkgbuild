# libpkgbuild design

## Authority position

libpkgsource owns normalized package-source declarations. libpkgcatalog owns
collection ordering and candidate authority. libpkgstate owns installed package
state. libpkgresolve selects exact package authorities and records requirement
edges. libpkgbuild consumes those sealed facts and owns only the logical build
request and build outcome.

The core does not discover, resolve, acquire, materialize, execute, inspect,
plan, install, or publish. A path is never build authority. An arbitrary digest
provided by a caller is never evidence that a filesystem tree was observed.

## Build subject

A build request names one package-selection identity in a complete
pkgresolve::resolution_result. Admission requires a catalog-backed selected
package whose retained candidate, source snapshot, release, package reference,
environment, and architecture context agree with the resolution request.
Installed-state selections cannot become build subjects because they do not
retain source authority.

## Direct build inputs

The selected source snapshot owns the exact expanded build and check
requirements. libpkgresolve owns the corresponding requirement edges and
required package selections. libpkgbuild admits the one-to-one relationship
between those authorities and derives a canonical build_input_set.

A build_input retains:

* whether the source-owned requirement is build or check scoped;
* the exact resolver requirement edge;
* the exact required selected package; and
* an identity over that relationship.

The input set contains direct build/check inputs only. Recursive construction
order and cycle detection belong to libpkgtransaction. Concrete package trees,
resource paths, and execution mounts belong to an execution-session boundary.

## Source materialization

Source declarations and content digests remain inside the source snapshot.
Observed and staged source bytes are owned by libpkgfetch and admitted by
libpkgbuild-exec. libpkgbuild has no duplicate "materialized source" value and
no caller-forgeable observation digest.

## Architecture and policy

The resolver-selected build and target architectures are validated against the
source-owned architecture requirements. Empty source architecture sets remain
unrestricted exactly as defined by libpkgsource; libpkgbuild must not reinterpret
them as an empty allow-list. The request retains only the selected pair; it does
not copy the source declaration vectors.

The environment policy is closed and typed: C.UTF-8, UTC, denied network,
isolated home, parallelism, umask, and optional SOURCE_DATE_EPOCH. The build
policy additionally selects the package-root output layout. Arbitrary ambient
environment maps are not authority.

## Build request identity

The first canonical build-request protocol binds:

* complete source-snapshot identity;
* admitted build-input-set identity;
* selected build architecture;
* selected target architecture; and
* build-policy identity.

Recipe release, program, profiles, requirements, sources, and architecture
constraints are not hashed again because the source-snapshot identity already
binds them.

## Build result

A successful result requires the exact request, execution-evidence identity,
complete intended payload manifest, and exact sealed artifact. It derives an
artifact binding and a complete result identity. A failed result requires
failure evidence and carries no payload, artifact, or artifact binding.

The payload model retains canonical path, object type, mode, numeric ownership,
size, modification time, regular-content digest, symbolic-link target,
hard-link topology, and device number. Hard-link entries name an earlier
regular anchor and retain the same inode mode, ownership, and modification time.
The artifact retains encoding, compression, exact byte count, and complete
SHA-256. A pathname is not part of
artifact authority.

A build result states intended payload and artifact bytes. It does not state
that decoding those bytes yields that payload. That pure cross-boundary
admission belongs to libpkgbuild-image. Planner projection belongs to the
standalone libpkgbuild-plan repository.

## ABI discipline

build_input, build_input_set, architecture_binding, build_request, and
build_result use opaque immutable storage. Their public layouts do not contain
foreign source, catalog, state, or resolver objects by value.

This prevents a foreign layout change from recursively corrupting downstream
objects. It does not erase semantic compatibility requirements: generated
pkg-config metadata constrains the supported libpkgsource and libpkgresolve
major generations, and the libpkgbuild SONAME changes when this public ABI
changes.

The reviewed ELF export manifest is authoritative. Hidden visibility, public
header compilation, shared/static qualification, and exact ABI-surface tests
prevent accidental publication.

## Non-goals

libpkgbuild contains no recipe parser, collection scanner, dependency solver,
source downloader, package-input materializer, process executor, namespace or
sandbox provider, archive writer or reader, planner adapter, state adapter,
application backend, historical importer, or legacy pkgmk compatibility layer.
