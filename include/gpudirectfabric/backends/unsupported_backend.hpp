#pragma once

#include "gpudirectfabric/backend.hpp"

#include <memory>
#include <string>

namespace gpudirectfabric {

// A backend that exposes NO real direct path: every capability, device, endpoint
// and transfer is classified UNSUPPORTED. Used to honestly represent RDMA,
// GPUDirect Storage, or any optional provider that is not present on the local
// platform. It never fabricates a GPUDirect claim.
class UnsupportedBackend : public Backend {
 public:
  UnsupportedBackend(std::string name, std::string provider_name, Provenance provenance);

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
  std::string name_;
  std::string provider_name_;
  Provenance provenance_;
};

std::shared_ptr<Backend> make_rdma_backend();      // RDMA/verbs provider: UNSUPPORTED here
std::shared_ptr<Backend> make_storage_backend();   // GPUDirect Storage provider: UNSUPPORTED here
std::shared_ptr<Backend> make_noop_backend();      // generic no-op backend

}  // namespace gpudirectfabric
