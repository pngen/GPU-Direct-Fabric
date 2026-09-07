#include "gpudirectfabric/coordinator.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace gpudirectfabric {

namespace {

// Helper to produce a transfer receipt.
TransferReceipt make_receipt(const TransferAttempt& a, TransferState state, bool integrity_ok,
                             const std::string& method, const std::string& detail) {
  TransferReceipt r;
  r.attempt_id = a.id;
  r.plan_generation = a.plan_generation;
  r.state = state;
  r.bytes = a.bytes;
  r.integrity_verified = integrity_ok;
  r.support = a.support;
  r.provenance = a.provenance;
  r.method = method;
  r.epoch = a.epoch;
  r.detail = detail;
  return r;
}

}  // namespace

Result<TransferAttempt> Coordinator::begin_transfer(TransferPlanId plan_id, const Authority& authority,
                                                    Backend* backend) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto pit = state_.plans.find(plan_id.value());
  if (pit == state_.plans.end()) {
    return err<TransferAttempt>(ErrorCode::NOT_FOUND, "transfer plan not found");
  }
  const TransferPlan& plan = pit->second;
  if (!plan.authoritative) {
    return err<TransferAttempt>(ErrorCode::STALE_GENERATION, "transfer plan is superseded");
  }
  if (plan.epoch.value() != state_.epoch.value()) {
    return err<TransferAttempt>(ErrorCode::STALE_EPOCH, "transfer plan epoch is stale");
  }
  if (authority.worker_owned()) {
    if (!worker_live_locked(authority)) {
      return err<TransferAttempt>(ErrorCode::STALE_BOOT, "begin authority is not live");
    }
    if (plan.authority.worker_owned() && authority != plan.authority) {
      return err<TransferAttempt>(ErrorCode::STALE_BOOT, "begin authority does not match plan authority");
    }
  }
  if (state_.attempts.size() >= state_.max_attempts) {
    return err<TransferAttempt>(ErrorCode::RESOURCE_LIMIT, "attempt limit reached");
  }

  // Re-validate generations are current.
  auto bit = state_.buffers.find(plan.source.value());
  if (bit == state_.buffers.end() || bit->second.generation.value() != plan.source_generation.value()) {
    return err<TransferAttempt>(ErrorCode::STALE_GENERATION, "source generation advanced since plan");
  }
  auto eit = state_.endpoints.find(plan.destination.value());
  if (eit == state_.endpoints.end() || eit->second.generation.value() != plan.destination_generation.value()) {
    return err<TransferAttempt>(ErrorCode::STALE_ENDPOINT, "destination generation advanced since plan");
  }
  if (plan.registration_id.valid()) {
    auto rit = state_.registrations.find(plan.registration_id.value());
    if (rit == state_.registrations.end() || rit->second.generation.value() != plan.registration_generation.value() ||
        rit->second.state != RegistrationState::REGISTERED) {
      return err<TransferAttempt>(ErrorCode::REGISTRATION_STALE, "registration stale for transfer");
    }
  }

  TransferAttempt attempt;
  attempt.id = TransferAttemptId(next_id_locked());
  attempt.plan_id = plan.id;
  attempt.plan_generation = plan.generation;
  attempt.state = TransferState::IN_FLIGHT;
  attempt.epoch = state_.epoch;
  attempt.authority = authority;
  attempt.submitted_bytes = plan.expected_bytes;
  attempt.support = plan.support;
  attempt.provenance = plan.provenance;

  // Run the backend data plane (narrow, accelerator-memory side only).
  if (backend != nullptr) {
    TransferRun run;
    run.plan = plan;
    run.accelerator_buffer = plan.source;
    run.expected_bytes = plan.expected_bytes;
    if (bit != state_.buffers.end()) run.accelerator_handle = bit->second.handle;
    auto outcome = backend->run_transfer(run);
    if (outcome) {
      attempt.bytes = outcome->bytes;
      attempt.integrity_checked = outcome->integrity_checked;
      attempt.integrity_passed = outcome->integrity_passed;
      attempt.method = outcome->method;
      attempt.support = outcome->support;
      attempt.provenance = outcome->provenance;
      attempt.detail = outcome->detail;
      attempt.backend_token = std::to_string(outcome->bytes);
      if (outcome->state == TransferState::FAILED) {
        attempt.state = TransferState::FAILED;
        attempt.detail = outcome->detail.empty() ? "backend transfer failed" : outcome->detail;
      }
    } else {
      attempt.state = TransferState::FAILED;
      attempt.detail = outcome.error().message;
    }
  }

  state_.attempts[attempt.id.value()] = attempt;
  return ok(attempt);
}

Result<TransferReceipt> Coordinator::complete_transfer(TransferAttemptId attempt_id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto ait = state_.attempts.find(attempt_id.value());
  if (ait == state_.attempts.end()) {
    return err<TransferReceipt>(ErrorCode::NOT_FOUND, "transfer attempt not found");
  }
  TransferAttempt& attempt = ait->second;

  auto rit = state_.receipts.find(attempt_id.value());
  if (attempt.completion_committed && rit != state_.receipts.end()) {
    return ok(rit->second);  // idempotent completion
  }
  if (attempt.state == TransferState::CANCELLED) {
    return err<TransferReceipt>(ErrorCode::CANCELLED, "cannot complete a cancelled transfer");
  }
  if (attempt.state == TransferState::FAILED) {
    if (rit != state_.receipts.end()) return ok(rit->second);
    TransferReceipt fr = make_receipt(attempt, TransferState::FAILED, false, attempt.method, attempt.detail);
    state_.receipts[attempt_id.value()] = fr;
    return ok(fr);
  }
  if (authority.worker_owned() && (!worker_live_locked(authority) || authority != attempt.authority)) {
    return err<TransferReceipt>(ErrorCode::STALE_BOOT, "completion authority mismatch");
  }

  auto pit = state_.plans.find(attempt.plan_id.value());
  if (pit == state_.plans.end() || !pit->second.authoritative) {
    return err<TransferReceipt>(ErrorCode::STALE_GENERATION, "plan superseded; completion is stale");
  }

  // Integrity gate: a completed real transfer must verify integrity.
  if (attempt.integrity_checked && !attempt.integrity_passed) {
    attempt.state = TransferState::FAILED;
    attempt.completion_committed = true;
    TransferReceipt fr = make_receipt(attempt, TransferState::FAILED, false, attempt.method,
                                      "integrity verification failed");
    state_.receipts[attempt_id.value()] = fr;
    VerificationReceipt vr;
    vr.id = VerificationId(next_id_locked());
    vr.attempt_id = attempt.id;
    vr.passed = false;
    vr.bytes_checked = attempt.bytes;
    vr.support = attempt.support;
    vr.method = attempt.method;
    vr.expected = "reference";
    vr.actual = "mismatch";
    state_.verifications[vr.id.value()] = vr;
    return err<TransferReceipt>(ErrorCode::INTEGRITY_FAILURE, "data integrity verification failed");
  }

  const bool completed = (attempt.submitted_bytes == 0) || (attempt.bytes == attempt.submitted_bytes);
  attempt.state = completed ? TransferState::COMPLETED : TransferState::FAILED;
  attempt.completion_committed = true;
  TransferReceipt receipt = make_receipt(attempt, attempt.state, attempt.integrity_checked, attempt.method,
                                         attempt.detail);
  state_.receipts[attempt_id.value()] = receipt;

  if (attempt.state == TransferState::COMPLETED && attempt.integrity_checked && attempt.integrity_passed) {
    VerificationReceipt vr;
    vr.id = VerificationId(next_id_locked());
    vr.attempt_id = attempt.id;
    vr.passed = true;
    vr.bytes_checked = attempt.bytes;
    vr.support = attempt.support;
    vr.method = attempt.method;
    vr.expected = "reference";
    vr.actual = "reference";
    state_.verifications[vr.id.value()] = vr;
  }
  return ok(receipt);
}

Result<TransferReceipt> Coordinator::cancel_transfer(TransferAttemptId attempt_id, const Authority& authority) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto ait = state_.attempts.find(attempt_id.value());
  if (ait == state_.attempts.end()) {
    return err<TransferReceipt>(ErrorCode::NOT_FOUND, "transfer attempt not found");
  }
  TransferAttempt& attempt = ait->second;
  if (attempt.completion_committed || attempt.state == TransferState::COMPLETED ||
      attempt.state == TransferState::VERIFIED) {
    return err<TransferReceipt>(ErrorCode::CANCELLED, "cannot cancel an already completed transfer");
  }
  if (authority.worker_owned() && (!worker_live_locked(authority) || authority != attempt.authority)) {
    return err<TransferReceipt>(ErrorCode::STALE_BOOT, "cancel authority mismatch");
  }
  attempt.state = TransferState::CANCELLED;
  attempt.completion_committed = false;
  TransferReceipt receipt = make_receipt(attempt, TransferState::CANCELLED, false, attempt.method,
                                         "transfer cancelled before completion");
  state_.receipts[attempt_id.value()] = receipt;
  return ok(receipt);
}

Result<VerificationReceipt> Coordinator::verify_transfer(TransferAttemptId attempt_id, bool passed,
                                                         std::uint64_t bytes_checked) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto ait = state_.attempts.find(attempt_id.value());
  if (ait == state_.attempts.end()) {
    return err<VerificationReceipt>(ErrorCode::NOT_FOUND, "transfer attempt not found");
  }
  const TransferAttempt& attempt = ait->second;
  VerificationReceipt vr;
  vr.id = VerificationId(next_id_locked());
  vr.attempt_id = attempt.id;
  vr.passed = passed;
  vr.bytes_checked = bytes_checked;
  vr.support = attempt.support;
  vr.method = attempt.method.empty() ? "explicit" : attempt.method;
  vr.expected = passed ? "reference" : "reference";
  vr.actual = passed ? "reference" : "mismatch";
  state_.verifications[vr.id.value()] = vr;
  return ok(vr);
}

Result<std::vector<TransferPlan>> Coordinator::plans() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<TransferPlan> out;
  out.reserve(state_.plans.size());
  for (const auto& [k, v] : state_.plans) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const TransferPlan& a, const TransferPlan& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<std::vector<TransferAttempt>> Coordinator::attempts() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<TransferAttempt> out;
  out.reserve(state_.attempts.size());
  for (const auto& [k, v] : state_.attempts) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const TransferAttempt& a, const TransferAttempt& b) {
    return a.id.value() < b.id.value();
  });
  return ok(std::move(out));
}

Result<std::vector<TransferReceipt>> Coordinator::receipts() const {
  std::lock_guard<std::mutex> guard(mutex_);
  std::vector<TransferReceipt> out;
  out.reserve(state_.receipts.size());
  for (const auto& [k, v] : state_.receipts) {
    (void)k;
    out.push_back(v);
  }
  std::sort(out.begin(), out.end(), [](const TransferReceipt& a, const TransferReceipt& b) {
    return a.attempt_id.value() < b.attempt_id.value();
  });
  return ok(std::move(out));
}

Result<TransferReceipt> Coordinator::latest_receipt(TransferAttemptId attempt) const {
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = state_.receipts.find(attempt.value());
  if (it == state_.receipts.end()) {
    return err<TransferReceipt>(ErrorCode::NOT_FOUND, "no receipt for attempt");
  }
  return ok(it->second);
}

}  // namespace gpudirectfabric