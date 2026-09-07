#include "gpudirectfabric/worker_client.hpp"

#include <cstring>

namespace gpudirectfabric {

bool WorkerClient::connect(unsigned short port) {
  return conn_.connect_to("127.0.0.1", port);
}

bool WorkerClient::hello_and_register(std::uint64_t worker, std::uint64_t boot, std::uint64_t epoch) {
  worker_ = worker; boot_ = boot; epoch_ = epoch;
  Frame h;
  h.type = MessageType::HELLO;
  pw_u64(h.payload, worker);
  pw_u64(h.payload, boot);
  pw_u64(h.payload, epoch);
  if (!conn_.send_frame(h)) return false;
  std::uint64_t a = 0, b = 0;
  return recv_ack(a, b);
}

bool WorkerClient::recv_ack(std::uint64_t& id1, std::uint64_t& id2) {
  Frame f;
  std::string err;
  if (!conn_.recv_frame(f, err)) return false;
  if (f.type == MessageType::NACK) return false;
  if (f.type != MessageType::ACK) return false;
  PayloadReader r{f.payload.data(), f.payload.size()};
  std::uint8_t status = r.u8();
  if (r.fail) return false;
  if (status != 0) return false;
  id1 = r.u64();
  id2 = r.u64();
  return !r.fail;
}

bool WorkerClient::publish_device(const std::string& name, const std::string& cc, bool peer, Support s,
                                  Provenance p, std::uint64_t& device_id) {
  Frame f;
  f.type = MessageType::PUBLISH_DEVICE;
  pw_str(f.payload, name);
  pw_str(f.payload, cc);
  pw_u8(f.payload, peer ? 1 : 0);
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(s)));
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(p)));
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  if (!recv_ack(a, b)) return false;
  device_id = a;
  return true;
}

bool WorkerClient::publish_endpoint(EndpointClass cls, const std::string& name, const std::string& top,
                                    bool healthy, Support s, Provenance p, std::uint64_t& endpoint_id) {
  Frame f;
  f.type = MessageType::PUBLISH_ENDPOINT;
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(cls)));
  pw_str(f.payload, name);
  pw_str(f.payload, top);
  pw_u8(f.payload, healthy ? 1 : 0);
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(s)));
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(p)));
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  if (!recv_ack(a, b)) return false;
  endpoint_id = a;
  return true;
}

bool WorkerClient::publish_capability(const std::string& provider, const std::string& cap,
                                      const std::string& val, Support s, Provenance p) {
  Frame f;
  f.type = MessageType::PUBLISH_CAPABILITY;
  pw_str(f.payload, provider);
  pw_str(f.payload, cap);
  pw_str(f.payload, val);
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(s)));
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(p)));
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  return recv_ack(a, b);
}

bool WorkerClient::create_buffer(MemoryDomain domain, std::uint64_t size, std::uint64_t align,
                                 std::uint64_t device, Support s, Provenance p, std::uint64_t& buffer_id) {
  Frame f;
  f.type = MessageType::CREATE_BUFFER;
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(domain)));
  pw_u64(f.payload, size);
  pw_u64(f.payload, align);
  pw_u64(f.payload, device);
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(s)));
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(p)));
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  if (!recv_ack(a, b)) return false;
  buffer_id = a;
  return true;
}

bool WorkerClient::register_buffer(std::uint64_t buffer, std::uint64_t endpoint, Support s, Provenance p,
                                   std::uint64_t& reg_id) {
  Frame f;
  f.type = MessageType::REGISTER_BUFFER;
  pw_u64(f.payload, buffer);
  pw_u64(f.payload, endpoint);
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(s)));
  pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(p)));
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  if (!recv_ack(a, b)) return false;
  reg_id = a;
  return true;
}

bool WorkerClient::ready() {
  Frame f;
  f.type = MessageType::READY;
  pw_u64(f.payload, worker_); pw_u64(f.payload, boot_); pw_u64(f.payload, epoch_);
  if (!conn_.send_frame(f)) return false;
  std::uint64_t a = 0, b = 0;
  return recv_ack(a, b);
}

void WorkerClient::close() { conn_.close(); }

}  // namespace gpudirectfabric