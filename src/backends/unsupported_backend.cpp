#include "gpudirectfabric/backends/unsupported_backend.hpp"

#include <string>

namespace gpudirectfabric {

UnsupportedBackend::UnsupportedBackend(std::string name, std::string provider_name, Provenance provenance)
    : name_(std::move(name)), provider_name_(std::move(provider_name)), provenance_(provenance) {}

std::string UnsupportedBackend::name() const { return name_; }
Support UnsupportedBackend::support() const noexcept { return Support::UNSUPPORTED; }
Provenance UnsupportedBackend::provenance() const noexcept { return provenance_; }
bool UnsupportedBackend::available() const noexcept { return false; }

std::vector<CapabilityObservation> UnsupportedBackend::capabilities() const {
  std::vector<CapabilityObservation> out;
  CapabilityObservation c;
  c.provider = provider_name_;
  c.capability = name_;
  c.value = "false";
  c.support = Support::UNSUPPORTED;
  c.provenance = provenance_;
  c.freshness = 0;
  out.push_back(c);
  return out;
}

std::vector<DeviceRecord> UnsupportedBackend::devices() const { return {}; }
std::vector<EndpointRecord> UnsupportedBackend::endpoints() const { return {}; }
std::vector<NicRecord> UnsupportedBackend::nics() const { return {}; }
std::vector<StorageEndpointRecord> UnsupportedBackend::storage_endpoints() const { return {}; }

std::vector<ProviderInfo> UnsupportedBackend::providers() const {
  ProviderInfo p;
  p.id = ProviderId(1);
  p.name = provider_name_;
  p.available = false;
  p.support = Support::UNSUPPORTED;
  p.provenance = provenance_;
  return {p};
}

Result<RegisterResult> UnsupportedBackend::register_buffer(const RegistrationSpec& spec) {
  (void)spec;
  return err<RegisterResult>(ErrorCode::UNSUPPORTED, "provider is not available on this platform");
}

Result<void> UnsupportedBackend::deregister_buffer(const Registration& reg) {
  (void)reg;
  return err<void>(ErrorCode::UNSUPPORTED, "provider is not available on this platform");
}

Result<TransferOutcome> UnsupportedBackend::run_transfer(const TransferRun& run) {
  (void)run;
  return err<TransferOutcome>(ErrorCode::UNSUPPORTED, "provider is not available on this platform");
}

std::shared_ptr<Backend> make_rdma_backend() {
  return std::make_shared<UnsupportedBackend>("rdma", "rdma", Provenance::RDMA_PROVIDER);
}
std::shared_ptr<Backend> make_storage_backend() {
  return std::make_shared<UnsupportedBackend>("storage", "storage", Provenance::STORAGE_BACKEND);
}
std::shared_ptr<Backend> make_noop_backend() {
  return std::make_shared<UnsupportedBackend>("unsupported", "unknown", Provenance::DERIVED);
}

}  // namespace gpudirectfabric
