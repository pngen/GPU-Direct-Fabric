#include "gpudirectfabric/transport.hpp"

#include <cstring>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>

namespace gpudirectfabric {

namespace {

bool winsock_ready() {
  static bool init = []() {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  return init;
}

bool recv_all(SOCKET s, void* buf, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    int r = ::recv(s, static_cast<char*>(buf) + got, static_cast<int>(n - got), 0);
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

}  // namespace

TcpConn::TcpConn(TcpConn&& other) noexcept : socket_(other.socket_), listener_(other.listener_) {
  other.socket_ = nullptr;
  other.listener_ = false;
}

TcpConn& TcpConn::operator=(TcpConn&& other) noexcept {
  if (this != &other) {
    close();
    socket_ = other.socket_;
    listener_ = other.listener_;
    other.socket_ = nullptr;
    other.listener_ = false;
  }
  return *this;
}

TcpConn::~TcpConn() { close(); }

bool TcpConn::connect_to(const char* host, unsigned short port) {
  if (!winsock_ready()) return false;
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(host);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), static_cast<int>(sizeof(addr))) != 0) {
    ::closesocket(s);
    return false;
  }
  socket_ = reinterpret_cast<void*>(s);
  listener_ = false;
  return true;
}

bool TcpConn::listen_on(unsigned short port, unsigned short& bound_port) {
  if (!winsock_ready()) return false;
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  int yes = 1;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), static_cast<int>(sizeof(addr))) != 0) {
    ::closesocket(s);
    return false;
  }
  if (::listen(s, SOMAXCONN) != 0) {
    ::closesocket(s);
    return false;
  }
  sockaddr_in local{};
  int len = static_cast<int>(sizeof(local));
  ::getsockname(s, reinterpret_cast<sockaddr*>(&local), &len);
  bound_port = ntohs(local.sin_port);
  socket_ = reinterpret_cast<void*>(s);
  listener_ = true;
  return true;
}

bool TcpConn::accept(TcpConn& out) {
  SOCKET s = reinterpret_cast<SOCKET>(socket_);
  if (!listener_) return false;
  SOCKET c = ::accept(s, nullptr, nullptr);
  if (c == INVALID_SOCKET) return false;
  out.close();
  out.socket_ = reinterpret_cast<void*>(c);
  out.listener_ = false;
  return true;
}

bool TcpConn::send_frame(const Frame& frame) {
  SOCKET s = reinterpret_cast<SOCKET>(socket_);
  if (s == INVALID_SOCKET || listener_) return false;
  std::vector<std::uint8_t> wire = encode_frame(frame);
  std::size_t sent = 0;
  while (sent < wire.size()) {
    int r = ::send(s, reinterpret_cast<const char*>(wire.data() + sent), static_cast<int>(wire.size() - sent), 0);
    if (r <= 0) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool TcpConn::recv_frame(Frame& frame, std::string& error) {
  SOCKET s = reinterpret_cast<SOCKET>(socket_);
  if (s == INVALID_SOCKET || listener_) { error = "not a connected socket"; return false; }
  std::vector<std::uint8_t> header(kProtoHeaderSize);
  if (!recv_all(s, header.data(), kProtoHeaderSize)) {
    error = "connection closed / truncated header";
    return false;
  }
  if (header[0] != 'G' || header[1] != 'D' || header[2] != 'F' || header[3] != '1') {
    error = "bad magic";
    return false;
  }
  std::uint16_t version = static_cast<std::uint16_t>(header[4]) | (static_cast<std::uint16_t>(header[5]) << 8);
  if (version != kProtoVersion) { error = "unsupported protocol version"; return false; }
  std::uint32_t payload_len = static_cast<std::uint32_t>(header[8]) |
                              (static_cast<std::uint32_t>(header[9]) << 8) |
                              (static_cast<std::uint32_t>(header[10]) << 16) |
                              (static_cast<std::uint32_t>(header[11]) << 24);
  if (payload_len > kProtoMaxPayload) { error = "absurd payload length"; return false; }
  std::vector<std::uint8_t> wire(header);
  wire.resize(kProtoHeaderSize + payload_len);
  if (payload_len > 0 && !recv_all(s, wire.data() + kProtoHeaderSize, payload_len)) {
    error = "connection closed / truncated payload";
    return false;
  }
  DecodeResult dr = decode_frame(wire);
  if (!dr.ok) { error = dr.error; return false; }
  frame = std::move(dr.frame);
  return true;
}

void TcpConn::close() {
  SOCKET s = reinterpret_cast<SOCKET>(socket_);
  if (s != INVALID_SOCKET && s != 0) {
    ::closesocket(s);
  }
  socket_ = nullptr;
  listener_ = false;
}

}  // namespace gpudirectfabric