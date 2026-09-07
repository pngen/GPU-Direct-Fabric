#include "gpudirectfabric/worker_client.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main(int argc, char** argv) {
  if (argc < 4) { std::printf("missing args\n"); return 2; }
  unsigned short port = static_cast<unsigned short>(std::atoi(argv[1]));
  std::uint64_t worker = std::strtoull(argv[2], nullptr, 10);
  std::uint64_t boot = std::strtoull(argv[3], nullptr, 10);
  gpudirectfabric::WorkerClient client;
  if (!client.connect(port)) { std::printf("connect-fail\n"); return 3; }
  if (!client.hello_and_register(worker, boot, 1)) { std::printf("hello-fail\n"); return 4; }
  std::uint64_t dev = 0, nic = 0, buf = 0, reg = 0;
  if (!client.publish_device("worker-gpu", "12.0", false, gpudirectfabric::Support::SYNTHETIC,
                             gpudirectfabric::Provenance::SYNTHETIC_FIXTURE, dev)) { return 5; }
  if (!client.publish_endpoint(gpudirectfabric::EndpointClass::NIC, "worker-nic", "pci", true,
                               gpudirectfabric::Support::SYNTHETIC,
                               gpudirectfabric::Provenance::SYNTHETIC_FIXTURE, nic)) { return 6; }
  if (!client.publish_capability("rdma", "direct_nic", "true", gpudirectfabric::Support::SYNTHETIC,
                                 gpudirectfabric::Provenance::SYNTHETIC_FIXTURE)) { return 7; }
  if (!client.publish_capability("synthetic", "host_staging", "true", gpudirectfabric::Support::SYNTHETIC,
                                 gpudirectfabric::Provenance::SYNTHETIC_FIXTURE)) { return 8; }
  if (!client.create_buffer(gpudirectfabric::MemoryDomain::CUDA_DEVICE, 1024 * 1024, 64, dev,
                            gpudirectfabric::Support::SYNTHETIC,
                            gpudirectfabric::Provenance::SYNTHETIC_FIXTURE, buf)) { return 9; }
  if (!client.register_buffer(buf, nic, gpudirectfabric::Support::SYNTHETIC,
                              gpudirectfabric::Provenance::SYNTHETIC_FIXTURE, reg)) { return 10; }
  if (!client.ready()) { return 11; }
  // Stay alive until the parent process kills this process.
  std::printf("WORKER_UP\n");
  std::fflush(stdout);
  for (;;) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
  return 0;
}
