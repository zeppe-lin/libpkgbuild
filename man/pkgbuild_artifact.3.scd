PKGBUILD_ARTIFACT(3)

# NAME

pkgbuild_artifact - exact published-byte evidence from libpkgbuild

# SYNOPSIS

*#include <pkgbuild/definition.hpp>*

*struct pkgbuild::SealedArtifactReceipt*;

# DESCRIPTION

A successful snapshot-bound libpkgbuild build retains two different records
about package archive publication.

*ArchiveReceipt* is package-writer operation evidence.  It records the output
pathname, writer-reported byte count, and archive encoding.

*SealedArtifactReceipt* is build-engine authority over the exact published
bytes.  It records:

. an absolute archive pathname used only as a transport label;
. the byte count observed through the retained regular-file descriptor; and
. a SHA-256 digest of those exact bytes.

After the writer returns, the engine verifies the requested and reported output
paths, opens the output without following a final symbolic link, hashes the
retained regular file, and rejects size or metadata change during sealing.

The pathname is not artifact identity.  Consumers requiring package artifact
semantics use *libpkgbuild-plan* together with *libpkgimage*.

# SEE ALSO

*pkgbuild_plan_adapter*(3)
