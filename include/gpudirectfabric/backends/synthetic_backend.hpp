#pragma once

#include "gpudirectfabric/backend.hpp"

#include <atomic>

namespace gpudirectfabric {

// A rigorous synthetic backend for semantics unavailable on the local machine.
// It NEVER fabricates REAL support; every observation and outcome is explicitly
// labeled SYNTHETIC and carries SYNTHETIC_FIXTURE provenance.
class SyntheticBackend : public Backend {
 public:
  SyntheticBackend() = default;

  // Whether run_transfer reports an integrity failure (for corruption tests).
  void set_corrupt(bool corrupt) noexcept { corrupt_ = corrupt; }
  [[nodiscard]] bool corrupt() const noexcept { return corrupt_; }

  std::string name() const override;
  Support support() const noexcept override;
  Provenance provenance() const noexcept override;
  bool available() const noexcept override;

  std::vector<CapabilityObservation> capabilities() const override;
  std::vector<DeviceRecord> devices() const override;
  std::vector<EndpointRecord> endpoints() const override;
  std::vector<NicRecord> nics() const override;
  std::vector<StorageEndpointRecord> storage_endpoints() const override;
  std::vector<ProviderInfo> providers() const override;

  Result<RegisterResult> register_buffer(const RegistrationSpec& spec) override;
  Result<void> deregister_buffer(const Registration& reg) override;
  Result<TransferOutcome> run_transfer(const TransferRun& run) override;

 private:
  bool corrupt_ = false;
  std::uint64_t next_reg_id_ = 1;
};

}  // namespace gpudirectfabric
