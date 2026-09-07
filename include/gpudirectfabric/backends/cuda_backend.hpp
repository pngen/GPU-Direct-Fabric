#pragma once

#include "gpudirectfabric/backend.hpp"

#include <string>
#include <vector>

namespace gpudirectfabric {

// Per-step result of the real CUDA validation proof.
struct CudaStepResult {
  bool pass = false;
  std::string detail;
};

struct CudaSelfTestReport {
  bool device_discovery = false;
  std::string device_name;
  std::string compute_capability;
  unsigned long long total_memory = 0;
  bool allocation = false;
  bool pinned_host = false;
  bool kernel = false;
  bool cpu_parity = false;
  bool baseline_return = false;
  Support support = Support::UNKNOWN;
  std::string summary;
};

// Real CUDA backend. It never fabricates GPUDirect RDMA / GDS support: it only
// exposes REAL observations about the CUDA device, GPU memory and pinned host
// memory. Direct NIC / storage paths are left as UNSUPPORTED.
class CudaBackend : public Backend {
 public:
  CudaBackend();
  ~CudaBackend() override = default;

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

  // Perform the full real CUDA proof: device discover, device + pinned-host
  // allocation, H2D, kernel, D2H, CPU parity, free, baseline return.
  CudaSelfTestReport self_test() const;

  void discover();

 private:
  std::vector<DeviceRecord> devices_;
  std::vector<ProviderInfo> providers_;
  int device_count_ = 0;
  bool discovered_ = false;
};

}  // namespace gpudirectfabric
