#pragma once

#include "gpudirectfabric/ids.hpp"

namespace gpudirectfabric {

class Coordinator;
class SyntheticBackend;

struct SyntheticSeedResult {
  DeviceId device{};
  EndpointId nic{};
  EndpointId storage{};
  EndpointId peer{};
  ProviderId rdma{};
  ProviderId storage_provider{};
  ProviderId cuda{};
};

// Seeds a coordinator with the synthetic backend's providers, device, endpoint
// and capability observations (all explicitly SYNTHETIC) and returns the
// coordinator-assigned ids so tests can wire buffers and paths to them.
SyntheticSeedResult seed_synthetic(Coordinator& coord, const SyntheticBackend& backend);

}  // namespace gpudirectfabric
