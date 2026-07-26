libpkgbuild history
===================

0.9.0
-----

The 0.9.0 release establishes exact build-artifact authority and the optional
planner adapter.

Core changes:

. snapshot-bound BuildReceipt now carries a SealedArtifactReceipt;
. the engine verifies the package writer output path and byte count;
. the engine opens the published regular file without following a final
  symbolic link;
. SHA-256 is computed over the exact retained bytes;
. mutation during sealing is rejected; and
. the core shared-library SONAME advances from 0 to 1 for the BuildReceipt ABI
  change.

Optional composition:

. libpkgbuild-plan projects the retained source snapshot through
  libpkgsource-plan 0.2.0;
. libpkgimage 0.3.0 reopens the artifact with the build digest as a mandatory
  exact-byte precondition;
. planner artifact identity identifies exact archive bytes;
. planner artifact-manifest identity binds source, candidate, archive, image,
  and inspection evidence; and
. libpkgbuild-plan begins at SONAME 0 as its first public ABI.

The archive pathname remains a label.  libpkgbuild does not derive package
identity from naming conventions and does not acquire package-planning or
installation authority.

0.8.8
-----

The 0.8.8 development release completed the snapshot-bound build-definition
seam, sealed private build workspaces, and differential parity composition over
source and artifact authorities.
