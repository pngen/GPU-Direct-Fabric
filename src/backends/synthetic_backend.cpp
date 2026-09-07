#include "gpudirectfabric/backends/synthetic_backend.hpp"
#include "gpudirectfabric/coordinator.hpp"
#include "gpudirectfabric/backends/synthetic_seed.hpp"

#include <memory>
#include <string>

namespace gpudirectfabric {

namespace {

class SyntheticHandle : public BackendHandle {
 public:
  explicit SyntheticHandle(std::uint64_t id) : id_(id) {}
  std::uint64_t opaque_id() const noexcept override { return id_; }
  std::string type_name() const override { return "synthetic"; }
  std::string describe() const override { return "synthetic handle #" + std::to_string(id_); }

 private:
  std::uint64_t id_;
};

}  // namespace

std::string SyntheticBackend::name() const { return "synthetic"; }
Support SyntheticBackend::support() const noexcept { return Support::SYNTHETIC; }
Provenance SyntheticBackend::provenance() const noexcept { return Provenance::SYNTHETIC_FIXTURE; }
bool SyntheticBackend::available() const noexcept { return true; }

std::vector<CapabilityObservation> SyntheticBackend::capabilities() const {
  std::vector<CapabilityObservation> out;
  const auto add = [&](const char* prov, const char* cap, const char* val) {
    CapabilityObservation c;
    c.provider = prov;
    c.capability = cap;
    c.value = val;
    c.support = Support::SYNTHETIC;
    c.provenance = Provenance::SYNTHETIC_FIXTURE;
    c.freshness = 0;
    out.push_back(c);
  };
  add("rdma", "direct_nic", "true");
  add("storage", "direct_storage", "true");
  add("cuda", "peer_memory", "true");
  add("synthetic", "host_staging", "true");
  return out;
}

std::vector<DeviceRecord> SyntheticBackend::devices() const {
  DeviceRecord d;
  d.id = DeviceId(1);
  d.generation = DeviceGeneration(1);
  d.name = "synthetic-gpu-0";
  d.compute_capability = "synthetic";
  d.peer_memory_supported = true;
  d.support = Support::SYNTHETIC;
  d.provenance = Provenance::SYNTHETIC_FIXTURE;
  d.freshness = 0;
  return {d};
}

std::vector<EndpointRecord> SyntheticBackend::endpoints() const {
  std::vector<EndpointRecord> out;
  EndpointRecord nic;
  nic.id = EndpointId(1);
  nic.generation = EndpointGeneration(1);
  nic.endpoint_class = EndpointClass::NIC;
  nic.name = "synthetic-nic-0";
  nic.topology_hint = "synthetic:pci";
  nic.healthy = true;
  nic.provider = ProviderId(1);
  nic.support = Support::SYNTHETIC;
  nic.provenance = Provenance::SYNTHETIC_FIXTURE;

  EndpointRecord storage;
  storage.id = EndpointId(2);
  storage.generation = EndpointGeneration(1);
  storage.endpoint_class = EndpointClass::STORAGE;
  storage.name = "synthetic-storage-0";
  storage.topology_hint = "synthetic:nvme";
  storage.healthy = true;
  storage.provider = ProviderId(2);
  storage.support = Support::SYNTHETIC;
  storage.provenance = Provenance::SYNTHETIC_FIXTURE;

  EndpointRecord peer;
  peer.id = EndpointId(3);
  peer.generation = EndpointGeneration(1);
  peer.endpoint_class = EndpointClass::PEER_DEVICE;
  peer.name = "synthetic-peer-gpu-1";
  peer.topology_hint = "synthetic:p2p";
  peer.healthy = true;
  peer.provider = ProviderId(3);
  peer.support = Support::SYNTHETIC;
  peer.provenance = Provenance::SYNTHETIC_FIXTURE;

  out.push_back(nic);
  out.push_back(storage);
  out.push_back(peer);
  return out;
}

std::vector<NicRecord> SyntheticBackend::nics() const {
  NicRecord n;
  n.id = NicId(1);
  n.generation = NicGeneration(1);
  n.name = "synthetic-nic-0";
  n.locator = "synthetic:0";
  n.rdma_capable = true;
  n.direct_path_capable = true;
  n.provider = ProviderId(1);
  n.support = Support::SYNTHETIC;
  n.provenance = Provenance::SYNTHETIC_FIXTURE;
  return {n};
}

std::vector<StorageEndpointRecord> SyntheticBackend::storage_endpoints() const {
  StorageEndpointRecord s;
  s.id = StorageEndpointId(1);
  s.generation = StorageGeneration(1);
  s.name = "synthetic-storage-0";
  s.backend_name = "synthetic-nvme";
  s.path = "synthetic:/dev/nvme0";
  s.direct_path_capable = true;
  s.provider = ProviderId(2);
  s.support = Support::SYNTHETIC;
  s.provenance = Provenance::SYNTHETIC_FIXTURE;
  return {s};
}

std::vector<ProviderInfo> SyntheticBackend::providers() const {
  std::vector<ProviderInfo> out;
  ProviderInfo rdma;
  rdma.id = ProviderId(1);
  rdma.name = "rdma";
  rdma.available = true;
  rdma.support = Support::SYNTHETIC;
  rdma.provenance = Provenance::SYNTHETIC_FIXTURE;
  ProviderInfo storage;
  storage.id = ProviderId(2);
  storage.name = "storage";
  storage.available = true;
  storage.support = Support::SYNTHETIC;
  storage.provenance = Provenance::SYNTHETIC_FIXTURE;
  ProviderInfo cuda;
  cuda.id = ProviderId(3);
  cuda.name = "cuda";
  cuda.available = true;
  cuda.support = Support::SYNTHETIC;
  cuda.provenance = Provenance::SYNTHETIC_FIXTURE;
  out.push_back(rdma);
  out.push_back(storage);
  out.push_back(cuda);
  return out;
}

Result<RegisterResult> SyntheticBackend::register_buffer(const RegistrationSpec& spec) {
  (void)spec;
  RegisterResult rr;
  rr.id = RegistrationId(next_reg_id_++);
  rr.generation = RegistrationGeneration(1);
  rr.support = Support::SYNTHETIC;
  rr.provenance = Provenance::SYNTHETIC_FIXTURE;
  rr.handle = std::make_shared<SyntheticHandle>(rr.id.value());
  return ok(rr);
}

Result<void> SyntheticBackend::deregister_buffer(const Registration& reg) {
  (void)reg;
  return ok();
}

Result<TransferOutcome> SyntheticBackend::run_transfer(const TransferRun& run) {
  TransferOutcome o;
  o.bytes = run.expected_bytes;
  o.integrity_checked = true;
  o.integrity_passed = !corrupt_;
  o.method = "synthetic-cpu-replica";
  o.support = Support::SYNTHETIC;
  o.provenance = Provenance::SYNTHETIC_FIXTURE;
  o.state = corrupt_ ? TransferState::FAILED : TransferState::COMPLETED;
  o.detail = corrupt_ ? "synthetic payload corruption detected" : "synthetic transfer completed";
  return ok(o);
}

SyntheticSeedResult seed_synthetic(Coordinator& coord, const SyntheticBackend& backend) {
  SyntheticSeedResult r{};
  const CoordinatorEpoch epoch = coord.epoch();

  for (const auto& p : backend.providers()) {
    ProviderAddParams pp;
    pp.name = p.name;
    pp.available = p.available;
    pp.support = p.support;
    pp.provenance = p.provenance;
    auto res = coord.add_provider(pp);
    if (res) {
      if (p.name == "rdma") r.rdma = res->id;
      if (p.name == "storage") r.storage_provider = res->id;
      if (p.name == "cuda") r.cuda = res->id;
    }
  }
  for (const auto& d : backend.devices()) {
    DeviceAddParams dp;
    dp.name = d.name;
    dp.compute_capability = d.compute_capability;
    dp.peer_memory_supported = d.peer_memory_supported;
    dp.support = d.support;
    dp.provenance = d.provenance;
    auto res = coord.add_device(dp);
    if (res) r.device = res->id;
  }
  for (const auto& e : backend.endpoints()) {
    EndpointAddParams ep;
    ep.endpoint_class = e.endpoint_class;
    ep.name = e.name;
    ep.topology_hint = e.topology_hint;
    ep.healthy = e.healthy;
    ep.provider = e.provider;
    ep.support = e.support;
    ep.provenance = e.provenance;
    auto res = coord.add_endpoint(ep);
    if (res) {
      if (e.endpoint_class == EndpointClass::NIC) r.nic = res->id;
      if (e.endpoint_class == EndpointClass::STORAGE) r.storage = res->id;
      if (e.endpoint_class == EndpointClass::PEER_DEVICE) r.peer = res->id;
    }
  }
  for (const auto& c : backend.capabilities()) {
    CapabilityPublishParams cp;
    cp.provider = c.provider;
    cp.capability = c.capability;
    cp.value = c.value;
    cp.support = c.support;
    cp.provenance = c.provenance;
    Authority a;
    a.epoch = epoch;  // synthetic capabilities are coordinator-owned in this epoch
    cp.authority = a;
    coord.publish_capability(cp);
  }
  return r;
}

}  // namespace gpudirectfabric
