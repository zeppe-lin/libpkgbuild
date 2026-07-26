// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/result.h>

#include "identity_support.h"

#include <utility>

namespace pkgbuild {
namespace {

artifact_binding_identity binding_id(const build_request& request,
                                     const payload_manifest& payload,
                                     const sealed_artifact& artifact)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/artifact-binding/v1");
  writer.text(request.identity().hex());
  writer.text(payload.identity().hex());
  writer.text(artifact.identity().hex());
  writer.text(artifact.complete_digest().hex());
  return artifact_binding_identity::from_sha256(writer.finish());
}

build_result_identity success_id(
    const build_request& request,
    const payload_manifest& payload,
    const sealed_artifact& artifact,
    const artifact_binding_identity& binding,
    const execution_evidence_identity& execution)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-result/v1");
  writer.text(to_string(build_outcome::succeeded));
  writer.text(request.identity().hex());
  writer.text(payload.identity().hex());
  writer.text(artifact.identity().hex());
  writer.text(binding.hex());
  writer.text(execution.hex());
  return build_result_identity::from_sha256(writer.finish());
}

build_result_identity failure_id(
    const build_request& request,
    const execution_evidence_identity& execution,
    const failure_evidence_identity& failure)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/build-result/v1");
  writer.text(to_string(build_outcome::failed));
  writer.text(request.identity().hex());
  writer.text(execution.hex());
  writer.text(failure.hex());
  return build_result_identity::from_sha256(writer.finish());
}

} // namespace

build_result::build_result(
    build_outcome outcome, build_request request,
    execution_evidence_identity execution_evidence,
    std::optional<payload_manifest> payload,
    std::optional<sealed_artifact> artifact,
    std::optional<artifact_binding_identity> artifact_binding,
    std::optional<failure_evidence_identity> failure_evidence,
    build_result_identity identity)
    : outcome_(outcome), request_(std::move(request)),
      execution_evidence_(std::move(execution_evidence)),
      payload_(std::move(payload)), artifact_(std::move(artifact)),
      artifact_binding_(std::move(artifact_binding)),
      failure_evidence_(std::move(failure_evidence)),
      identity_(std::move(identity))
{
}

build_result build_result::succeeded(
    build_request request, payload_manifest payload,
    sealed_artifact artifact,
    execution_evidence_identity execution_evidence)
{
  auto binding = binding_id(request, payload, artifact);
  auto identity = success_id(request, payload, artifact, binding,
                             execution_evidence);
  return build_result(build_outcome::succeeded, std::move(request),
                      std::move(execution_evidence), std::move(payload),
                      std::move(artifact), std::move(binding), std::nullopt,
                      std::move(identity));
}

build_result build_result::failed(
    build_request request,
    execution_evidence_identity execution_evidence,
    failure_evidence_identity failure_evidence)
{
  auto identity = failure_id(request, execution_evidence, failure_evidence);
  return build_result(build_outcome::failed, std::move(request),
                      std::move(execution_evidence), std::nullopt,
                      std::nullopt, std::nullopt,
                      std::move(failure_evidence), std::move(identity));
}

build_outcome build_result::outcome() const noexcept { return outcome_; }
const build_request& build_result::request() const noexcept { return request_; }
const execution_evidence_identity& build_result::execution_evidence() const noexcept { return execution_evidence_; }
const std::optional<payload_manifest>& build_result::payload() const noexcept { return payload_; }
const std::optional<sealed_artifact>& build_result::artifact() const noexcept { return artifact_; }
const std::optional<artifact_binding_identity>& build_result::artifact_binding() const noexcept { return artifact_binding_; }
const std::optional<failure_evidence_identity>& build_result::failure_evidence() const noexcept { return failure_evidence_; }
const build_result_identity& build_result::identity() const noexcept { return identity_; }
bool operator==(const build_result& lhs, const build_result& rhs) noexcept { return lhs.identity_ == rhs.identity_; }
bool operator!=(const build_result& lhs, const build_result& rhs) noexcept { return !(lhs == rhs); }

} // namespace pkgbuild
