// SPDX-FileCopyrightText: 2026 Alexandr Savca
// SPDX-License-Identifier: GPL-3.0-or-later
#include <libpkgbuild/result.h>

#include "identity_support.h"

#include <memory>
#include <utility>

namespace pkgbuild {
namespace {

artifact_binding_identity binding_id(const build_request& request,
                                     const payload_manifest& payload,
                                     const sealed_artifact& artifact)
{
  detail::identity_writer writer;
  writer.text("libpkgbuild/artifact-binding/1");
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
  writer.text("libpkgbuild/build-result/1");
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
  writer.text("libpkgbuild/build-result/1");
  writer.text(to_string(build_outcome::failed));
  writer.text(request.identity().hex());
  writer.text(execution.hex());
  writer.text(failure.hex());
  return build_result_identity::from_sha256(writer.finish());
}

} // namespace

struct build_result::impl final {
  impl(build_outcome outcome_value,
       build_request request_value,
       execution_evidence_identity execution_value,
       std::optional<payload_manifest> payload_value,
       std::optional<sealed_artifact> artifact_value,
       std::optional<artifact_binding_identity> binding_value,
       std::optional<failure_evidence_identity> failure_value,
       build_result_identity identity_value)
      : outcome(outcome_value), request(std::move(request_value)),
        execution(std::move(execution_value)), payload(std::move(payload_value)),
        artifact(std::move(artifact_value)), binding(std::move(binding_value)),
        failure(std::move(failure_value)), identity(std::move(identity_value))
  {
  }
  build_outcome outcome;
  build_request request;
  execution_evidence_identity execution;
  std::optional<payload_manifest> payload;
  std::optional<sealed_artifact> artifact;
  std::optional<artifact_binding_identity> binding;
  std::optional<failure_evidence_identity> failure;
  build_result_identity identity;
};

build_result::build_result(std::shared_ptr<const impl> value)
    : impl_(std::move(value))
{
}
build_result::build_result(const build_result&) noexcept = default;
build_result::build_result(build_result&&) noexcept = default;
build_result& build_result::operator=(const build_result&) noexcept = default;
build_result& build_result::operator=(build_result&&) noexcept = default;
build_result::~build_result() = default;

build_result build_result::succeeded(
    build_request request, payload_manifest payload,
    sealed_artifact artifact,
    execution_evidence_identity execution_evidence)
{
  auto binding = binding_id(request, payload, artifact);
  auto identity = success_id(request, payload, artifact, binding,
                             execution_evidence);
  return build_result(std::make_shared<impl>(
      build_outcome::succeeded, std::move(request),
      std::move(execution_evidence), std::move(payload), std::move(artifact),
      std::move(binding), std::nullopt, std::move(identity)));
}

build_result build_result::failed(
    build_request request,
    execution_evidence_identity execution_evidence,
    failure_evidence_identity failure_evidence)
{
  auto identity = failure_id(request, execution_evidence, failure_evidence);
  return build_result(std::make_shared<impl>(
      build_outcome::failed, std::move(request),
      std::move(execution_evidence), std::nullopt, std::nullopt, std::nullopt,
      std::move(failure_evidence), std::move(identity)));
}

build_outcome build_result::outcome() const noexcept { return impl_->outcome; }
const build_request& build_result::request() const noexcept { return impl_->request; }
const execution_evidence_identity&
build_result::execution_evidence() const noexcept { return impl_->execution; }
const std::optional<payload_manifest>& build_result::payload() const noexcept
{ return impl_->payload; }
const std::optional<sealed_artifact>& build_result::artifact() const noexcept
{ return impl_->artifact; }
const std::optional<artifact_binding_identity>&
build_result::artifact_binding() const noexcept { return impl_->binding; }
const std::optional<failure_evidence_identity>&
build_result::failure_evidence() const noexcept { return impl_->failure; }
const build_result_identity& build_result::identity() const noexcept
{ return impl_->identity; }
bool operator==(const build_result& lhs, const build_result& rhs) noexcept
{ return lhs.identity() == rhs.identity(); }
bool operator!=(const build_result& lhs, const build_result& rhs) noexcept
{ return !(lhs == rhs); }

} // namespace pkgbuild
