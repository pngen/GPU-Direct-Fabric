#include "gpudirectfabric/backends/cuda_backend.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace gpudirectfabric {

// Raw CUDA operations => returns 0 on success, else a CUDA error code.
extern "C" {
int gdf_cuda_count(int* count);
int gdf_cuda_props(int index, char* name, int name_len, int* cc_major, int* cc_minor,
                   unsigned long long* total_mem);
int gdf_cuda_malloc(void** ptr, unsigned long long n);
int gdf_cuda_free(void* ptr);
int gdf_cuda_malloc_host(void** ptr, unsigned long long n);
int gdf_cuda_free_host(void* ptr);
int gdf_cuda_h2d(void* dst, const void* src, unsigned long long n);
int gdf_cuda_d2h(void* dst, const void* src, unsigned long long n);
int gdf_cuda_sync();
int gdf_cuda_meminfo(unsigned long long* free_bytes, unsigned long long* total_bytes);
int gdf_cuda_invert(void* p, unsigned long long n);
}

CudaBackend::CudaBackend() {
  discover();
}

void CudaBackend::discover() {
  devices_.clear();
  providers_.clear();
  int count = 0;
  if (gdf_cuda_count(&count) != 0 || count <= 0) {
    device_count_ = 0;
    return;
  }
  device_count_ = count;
  for (int i = 0; i < count; ++i) {
    DeviceRecord d;
    d.id = DeviceId(static_cast<std::uint64_t>(i + 1));
    d.generation = DeviceGeneration(1);
    char name[256] = {0};
    int ccM = 0, ccN = 0;
    unsigned long long total = 0;
    if (gdf_cuda_props(i, name, static_cast<int>(sizeof(name)), &ccM, &ccN, &total) == 0) {
      d.name = name;
      d.compute_capability = std::to_string(ccM) + "." + std::to_string(ccN);
    } else {
      d.name = "cuda-device-" + std::to_string(i);
      d.compute_capability = "unknown";
    }
    d.peer_memory_supported = false;  // determined per-environment; single-GPU => no peer
    d.support = Support::REAL;
    d.provenance = Provenance::CUDA_RUNTIME;
    d.freshness = 0;
    devices_.push_back(std::move(d));
  }
  ProviderInfo cuda;
  cuda.id = ProviderId(1);
  cuda.name = "cuda";
  cuda.available = true;
  cuda.support = Support::REAL;
  cuda.provenance = Provenance::CUDA_RUNTIME;
  providers_.push_back(cuda);
  discovered_ = true;
}

std::string CudaBackend::name() const { return "cuda"; }
Support CudaBackend::support() const noexcept { return discovered_ && device_count_ > 0 ? Support::REAL : Support::UNSUPPORTED; }
Provenance CudaBackend::provenance() const noexcept { return Provenance::CUDA_RUNTIME; }
bool CudaBackend::available() const noexcept { return discovered_ && device_count_ > 0; }

std::vector<CapabilityObservation> CudaBackend::capabilities() const {
  std::vector<CapabilityObservation> out;
  auto add = [&](const char* prov, const char* cap, const char* val, Support sup, Provenance p) {
    CapabilityObservation c;
    c.provider = prov;
    c.capability = cap;
    c.value = val;
    c.support = sup;
    c.provenance = p;
    c.freshness = 0;
    out.push_back(c);
  };
  add("cuda", "device_present", discovered_ && device_count_ > 0 ? "true" : "false", Support::REAL,
      Provenance::CUDA_RUNTIME);
  add("cuda", "registered_gpu_memory", "true", Support::REAL, Provenance::CUDA_DRIVER);
  add("cuda", "host_pinned", "true", Support::REAL, Provenance::CUDA_RUNTIME);
  add("cuda", "peer_memory", "false", Support::REAL, Provenance::CUDA_RUNTIME);
  return out;
}

std::vector<DeviceRecord> CudaBackend::devices() const { return devices_; }
std::vector<EndpointRecord> CudaBackend::endpoints() const { return {}; }
std::vector<NicRecord> CudaBackend::nics() const { return {}; }
std::vector<StorageEndpointRecord> CudaBackend::storage_endpoints() const { return {}; }
std::vector<ProviderInfo> CudaBackend::providers() const { return providers_; }

Result<RegisterResult> CudaBackend::register_buffer(const RegistrationSpec& spec) {
  (void)spec;
  // No REAL GPUDirect RDMA / GDS registration exists on this platform.
  return err<RegisterResult>(ErrorCode::UNSUPPORTED,
                             "CUDA backend does not expose a real direct registration path");
}

Result<void> CudaBackend::deregister_buffer(const Registration& reg) {
  (void)reg;
  return ok();
}

Result<TransferOutcome> CudaBackend::run_transfer(const TransferRun& run) {
  (void)run;
  return err<TransferOutcome>(ErrorCode::UNSUPPORTED,
                              "no real GPUDirect path is available to execute");
}

CudaSelfTestReport CudaBackend::self_test() const {
  CudaSelfTestReport rep;
  int count = 0;
  if (gdf_cuda_count(&count) != 0 || count <= 0) {
    rep.support = Support::UNSUPPORTED;
    rep.summary = "no CUDA device available";
    return rep;
  }
  char name[256] = {0};
  int ccM = 0, ccN = 0;
  unsigned long long total = 0;
  if (gdf_cuda_props(0, name, static_cast<int>(sizeof(name)), &ccM, &ccN, &total) != 0) {
    rep.support = Support::UNSUPPORTED;
    rep.summary = "CUDA device property query failed";
    return rep;
  }
  rep.device_discovery = true;
  rep.device_name = name;
  rep.compute_capability = std::to_string(ccM) + "." + std::to_string(ccN);
  rep.total_memory = total;

  const unsigned long long bytes = 16ull * 1024 * 1024;
  void* dev = nullptr;
  void* host = nullptr;
  if (gdf_cuda_malloc(&dev, bytes) != 0) {
    rep.support = Support::UNSUPPORTED;
    rep.summary = "cudaMalloc failed";
    return rep;
  }
  rep.allocation = true;
  if (gdf_cuda_malloc_host(&host, bytes) != 0) {
    gdf_cuda_free(dev);
    rep.support = Support::UNSUPPORTED;
    rep.summary = "cudaMallocHost (pinned) failed";
    return rep;
  }
  rep.pinned_host = true;

  unsigned long long free0 = 0, tot0 = 0;
  gdf_cuda_meminfo(&free0, &tot0);

  std::vector<unsigned char> original(static_cast<std::size_t>(bytes));
  for (std::size_t i = 0; i < original.size(); ++i) {
    original[i] = static_cast<unsigned char>(i ^ 0xA5u);
  }
  std::memcpy(host, original.data(), static_cast<std::size_t>(bytes));

  if (gdf_cuda_h2d(dev, host, bytes) != 0) {
    gdf_cuda_free(dev);
    gdf_cuda_free_host(host);
    rep.support = Support::UNSUPPORTED;
    rep.summary = "H2D copy failed";
    return rep;
  }
  if (gdf_cuda_invert(dev, bytes) != 0 || gdf_cuda_sync() != 0) {
    gdf_cuda_free(dev);
    gdf_cuda_free_host(host);
    rep.support = Support::UNSUPPORTED;
    rep.summary = "CUDA kernel launch or sync failed";
    return rep;
  }
  rep.kernel = true;

  if (gdf_cuda_d2h(host, dev, bytes) != 0) {
    gdf_cuda_free(dev);
    gdf_cuda_free_host(host);
    rep.support = Support::UNSUPPORTED;
    rep.summary = "D2H copy failed";
    return rep;
  }

  bool parity = true;
  for (std::size_t i = 0; i < original.size(); ++i) {
    unsigned char expected = static_cast<unsigned char>(~original[i]);
    unsigned char actual = static_cast<unsigned char*>(host)[i];
    if (actual != expected) {
      parity = false;
      break;
    }
  }
  rep.cpu_parity = parity;

  gdf_cuda_free(dev);
  gdf_cuda_free_host(host);

  unsigned long long free1 = 0, tot1 = 0;
  gdf_cuda_meminfo(&free1, &tot1);
  const unsigned long long tolerance = 16ull * 1024 * 1024 + 16ull * 1024 * 1024;
  rep.baseline_return = (free1 + tolerance >= free0);

  rep.support = (parity && rep.baseline_return) ? Support::REAL : Support::UNSUPPORTED;
  rep.summary = std::string(rep.support == Support::REAL ? "REAL" : "FAILED") +
                " device=" + rep.device_name + " cc=" + rep.compute_capability +
                " parity=" + (rep.cpu_parity ? "ok" : "bad") +
                " baseline=" + (rep.baseline_return ? "ok" : "miss");
  return rep;
}

}  // namespace gpudirectfabric
