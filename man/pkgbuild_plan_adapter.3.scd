PKGBUILD_PLAN_ADAPTER(3)

# NAME

pkgbuild_plan_adapter - project a sealed build into planner artifact truth

# SYNOPSIS

*#include <pkgbuild-plan/adapter.hpp>*

*pkgbuild::plan_adapter::artifact_projection*++
*pkgbuild::plan_adapter::project_artifact(* \
    *pkgbuild::BuildReceipt build,* \
    *const pkgimage::archive_backend& archives*);

# DESCRIPTION

*libpkgbuild-plan* is the optional destination adapter from completed build
evidence into *libpkgplan* artifact vocabulary.

*project_artifact()* retains the complete build receipt, projects its source
snapshot through *libpkgsource-plan*, and inspects the sealed artifact through
the supplied *libpkgimage* backend.  Inspection requires the exact SHA-256
digest issued by the build engine.

The returned *artifact_projection* contains:

. the complete build receipt;
. the lifetime-bound source candidate projection;
. the inspected package image and inspection receipt; and
. the planner artifact package fact.

The planner artifact identity identifies exact archive bytes.  The
artifact-manifest identity binds source snapshot provenance, candidate release
and control, exact bytes, normalized image identity, and inspection receipt
identity.

# ERRORS

Projection throws *projection_error*.  Its code distinguishes malformed build
receipt evidence, source projection failure, archive inspection failure, and
planner fact rejection.

# NOTES

The adapter does not plan a package operation, derive artifact identity from a
filename, or apply package effects.  The core *libpkgbuild* library does not
depend on *libpkgplan* or *libpkgimage*.

# SEE ALSO

*pkgbuild_artifact*(3), *libpkgimage*(3), *libpkgplan*(3),
*pkgsource_plan_adapter*(3)
