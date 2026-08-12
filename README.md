# libpkgbuild

libpkgbuild is the native authority for logical package-build requests and
build outcomes in Zeppe-Lin.

A build request admits one exact catalog-backed package selection from a sealed
libpkgresolve result. It derives the source snapshot, direct build/check inputs,
and selected architectures from that authority, then binds them to a closed
build policy. Callers cannot reconstruct resolver authority from loose release,
source, artifact, or filesystem identities.

A successful build result binds the request, execution evidence, a complete
intended payload, and exact artifact bytes. A failed result binds the request,
execution evidence, and failure evidence and cannot carry partial success data.

The core deliberately does not acquire sources, materialize package trees,
execute programs, inspect archives, project planner facts, publish installed
state, or apply payloads. Those are separate provider, execution, adapter, and
orchestration boundaries.

The public request/result values use opaque immutable storage. libpkgsource and
libpkgresolve remain explicit ABI dependencies, but their object layouts do not
propagate through downstream libpkgbuild consumers.

See DESIGN.md for the authority model and TESTING.md for qualification.
