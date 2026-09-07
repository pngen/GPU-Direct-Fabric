#pragma once

#include "gpudirectfabric/coordinator.hpp"
#include "gpudirectfabric/transport.hpp"

namespace gpudirectfabric {

// Runs a coordinator over the framed TCP loopback protocol. Accepts workers and
// query clients sequentially, applies PUBLISH_*/REGISTER_BUFFER/CREATE_BUFFER
// messages, and marks a worker dead when its connection drops (real process
// death detection via socket disconnect).
class CoordinatorServer {
 public:
  explicit CoordinatorServer(Coordinator& coord) : coord_(coord) {}
  // Blocks; returns 0 on clean shutdown, non-zero on error. Prints "PORT <n>".
  int run(unsigned short port);

 private:
  bool send_ack(TcpConn& conn, std::uint64_t id1 = 0, std::uint64_t id2 = 0, std::uint64_t id3 = 0);
  bool send_nack(TcpConn& conn, ErrorCode code, const std::string& msg);
  bool handle_worker_message(TcpConn& conn, const Frame& f, std::uint64_t worker, std::uint64_t boot,
                             std::uint64_t epoch);
  bool handle_query(TcpConn& conn, const Frame& f);
  bool send_state(TcpConn& conn);

  Coordinator& coord_;
};

}  // namespace gpudirectfabric