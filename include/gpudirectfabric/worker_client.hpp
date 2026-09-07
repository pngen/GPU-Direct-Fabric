#pragma once

#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/ids.hpp"
#include "gpudirectfabric/transport.hpp"

#include <cstdint>
#include <string>

namespace gpudirectfabric {

// A client for the worker side of the framed protocol. Owns a connection.
class WorkerClient {
 public:
  WorkerClient() = default;

  bool connect(unsigned short port);
  bool hello_and_register(std::uint64_t worker, std::uint64_t boot, std::uint64_t epoch);

  bool publish_device(const std::string& name, const std::string& cc, bool peer, Support s, Provenance p,
                      std::uint64_t& device_id);
  bool publish_endpoint(EndpointClass cls, const std::string& name, const std::string& top, bool healthy,
                        Support s, Provenance p, std::uint64_t& endpoint_id);
  bool publish_capability(const std::string& provider, const std::string& cap, const std::string& val,
                          Support s, Provenance p);
  bool create_buffer(MemoryDomain domain, std::uint64_t size, std::uint64_t align, std::uint64_t device,
                     Support s, Provenance p, std::uint64_t& buffer_id);
  bool register_buffer(std::uint64_t buffer, std::uint64_t endpoint, Support s, Provenance p,
                       std::uint64_t& reg_id);
  bool ready();
  void close();

 private:
  bool recv_ack(std::uint64_t& id1, std::uint64_t& id2);
  TcpConn conn_;
  std::uint64_t worker_ = 0, boot_ = 0, epoch_ = 0;
};

}  // namespace gpudirectfabric
