#pragma once

#include "gpudirectfabric/protocol.hpp"

#include <string>

namespace gpudirectfabric {

// A TCP loopback connection that exchanges length-prefixed, CRC-protected frames.
class TcpConn {
 public:
  TcpConn() = default;
  ~TcpConn();
  TcpConn(const TcpConn&) = delete;
  TcpConn& operator=(const TcpConn&) = delete;
  TcpConn(TcpConn&& other) noexcept;
  TcpConn& operator=(TcpConn&& other) noexcept;

  // Client: connect to host:port. Returns false on failure.
  bool connect_to(const char* host, unsigned short port);

  // Server: bind 127.0.0.1:port (port 0 => ephemeral). Reports the bound port.
  bool listen_on(unsigned short port, unsigned short& bound_port);

  // Server: accept a single incoming connection into `out`.
  bool accept(TcpConn& out);

  bool send_frame(const Frame& frame);
  // Reads exactly one frame; returns false and fills error on truncation/checksum.
  bool recv_frame(Frame& frame, std::string& error);

  void close();

 private:
  void* socket_ = nullptr;   // SOCKET, opaque
  bool listener_ = false;
};

}  // namespace gpudirectfabric