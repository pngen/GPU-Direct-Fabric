#include "gpudirectfabric/coordinator_server.hpp"

#include "gpudirectfabric/transport.hpp"

#include <cstdio>
#include <string>
#include <thread>
#include <utility>

namespace gpudirectfabric {

namespace {

std::uint8_t enum8(Support s) { return static_cast<std::uint8_t>(static_cast<int>(s)); }
Support support8(std::uint8_t v) { return static_cast<Support>(static_cast<int>(v)); }
std::uint8_t enum8(Provenance p) { return static_cast<std::uint8_t>(static_cast<int>(p)); }
Provenance provenance8(std::uint8_t v) { return static_cast<Provenance>(static_cast<int>(v)); }
std::uint8_t enum8(EndpointClass e) { return static_cast<std::uint8_t>(static_cast<int>(e)); }
EndpointClass endpoint8(std::uint8_t v) { return static_cast<EndpointClass>(static_cast<int>(v)); }
std::uint8_t enum8(MemoryDomain d) { return static_cast<std::uint8_t>(static_cast<int>(d)); }
MemoryDomain domain8(std::uint8_t v) { return static_cast<MemoryDomain>(static_cast<int>(v)); }

bool read_worker(PayloadReader& r, std::uint64_t& w, std::uint64_t& b, std::uint64_t& e) {
  w = r.u64(); b = r.u64(); e = r.u64();
  return !r.fail;
}

}  // namespace

bool CoordinatorServer::send_ack(TcpConn& conn, std::uint64_t id1, std::uint64_t id2, std::uint64_t id3) {
  Frame f;
  f.type = MessageType::ACK;
  pw_u8(f.payload, 0);  // status ok
  pw_u64(f.payload, id1);
  pw_u64(f.payload, id2);
  pw_u64(f.payload, id3);
  return conn.send_frame(f);
}

bool CoordinatorServer::send_nack(TcpConn& conn, ErrorCode code, const std::string& msg) {
  Frame f;
  f.type = MessageType::NACK;
  pw_u8(f.payload, 1);
  pw_u32(f.payload, static_cast<std::uint32_t>(static_cast<int>(code)));
  pw_u32(f.payload, static_cast<std::uint32_t>(msg.size()));
  for (char c : msg) pw_u8(f.payload, static_cast<std::uint8_t>(c));
  return conn.send_frame(f);
}

int CoordinatorServer::run(unsigned short port) {
  TcpConn listener;
  unsigned short bound = 0;
  if (!listener.listen_on(port, bound)) return 2;
  std::printf("PORT %u\n", bound);
  std::fflush(stdout);
  while (true) {
    TcpConn conn;
    if (!listener.accept(conn)) break;
    Frame hf;
    std::string err;
    if (!conn.recv_frame(hf, err)) { conn.close(); continue; }
    if (hf.type != MessageType::HELLO && hf.type != MessageType::REGISTER_WORKER) { conn.close(); continue; }
    PayloadReader r{hf.payload.data(), hf.payload.size()};
    std::uint64_t worker = 0, boot = 0, epoch = 0;
    read_worker(r, worker, boot, epoch);
    const bool is_worker = (worker != 0);
    if (is_worker) {
      // Register the worker and ACK the HELLO handshake before the per-connection
      // thread takes over the socket. A stale boot or epoch is rejected here.
      auto rw = this->coord_.register_worker(WorkerId(worker), WorkerBootId(boot), CoordinatorEpoch(epoch));
      if (!rw) {
        this->send_nack(conn, rw.error().code, rw.error().message);
        conn.close();
        continue;
      }
      send_ack(conn);
      std::thread([this, conn = std::move(conn), worker, boot, epoch]() mutable {
        while (true) {
          Frame f;
          std::string e;
          if (!conn.recv_frame(f, e)) break;
          if (!this->handle_worker_message(conn, f, worker, boot, epoch)) break;
        }
        this->coord_.mark_worker_dead(WorkerId(worker));
        conn.close();
      }).detach();
    } else {
      std::thread([this, conn = std::move(conn)]() mutable {
        while (true) {
          Frame f;
          std::string e;
          if (!conn.recv_frame(f, e)) break;
          if (f.type == MessageType::QUERY_STATE) {
            if (!this->send_state(conn)) break;
          } else {
            this->send_nack(conn, ErrorCode::UNSUPPORTED, "query connection only supports QUERY_STATE");
          }
        }
        conn.close();
      }).detach();
    }
  }
  listener.close();
  return 0;
}

bool CoordinatorServer::handle_worker_message(TcpConn& conn, const Frame& f, std::uint64_t worker,
                                               std::uint64_t boot, std::uint64_t epoch) {
  PayloadReader r{f.payload.data(), f.payload.size()};
  Authority auth;
  auth.worker = WorkerId(worker);
  auth.boot = WorkerBootId(boot);
  auth.epoch = CoordinatorEpoch(epoch);
  switch (f.type) {
    case MessageType::PUBLISH_DEVICE: {
      std::string name, cc;
      if (!r.str(name) || !r.str(cc)) return false;
      std::uint8_t peer = r.u8(), ssup = r.u8(), prov = r.u8();
      DeviceAddParams p;
      p.name = name; p.compute_capability = cc;
      p.peer_memory_supported = (peer != 0);
      p.support = support8(ssup); p.provenance = provenance8(prov);
      p.authority = auth;
      auto res = coord_.add_device(p);
      return res ? send_ack(conn, res->id.value()) : send_nack(conn, res.error().code, res.error().message);
    }
    case MessageType::PUBLISH_ENDPOINT: {
      std::uint8_t cls = r.u8();
      std::string nm, top;
      if (!r.str(nm) || !r.str(top)) return false;
      std::uint8_t healthy = r.u8(), ssup = r.u8(), prov = r.u8();
      EndpointAddParams p;
      p.endpoint_class = endpoint8(cls); p.name = nm; p.topology_hint = top;
      p.healthy = (healthy != 0); p.support = support8(ssup); p.provenance = provenance8(prov);
      p.authority = auth;
      auto res = coord_.add_endpoint(p);
      return res ? send_ack(conn, res->id.value()) : send_nack(conn, res.error().code, res.error().message);
    }
    case MessageType::PUBLISH_CAPABILITY: {
      std::string prov, cap, val;
      if (!r.str(prov) || !r.str(cap) || !r.str(val)) return false;
      std::uint8_t ssup = r.u8(), pr = r.u8();
      CapabilityPublishParams p;
      p.provider = prov; p.capability = cap; p.value = val;
      p.support = support8(ssup); p.provenance = provenance8(pr);
      p.authority = auth;
      auto res = coord_.publish_capability(p);
      return res ? send_ack(conn, res->id.value()) : send_nack(conn, res.error().code, res.error().message);
    }
    case MessageType::CREATE_BUFFER: {
      std::uint8_t dm = r.u8();
      std::uint64_t bsize = r.u64(), balign = r.u64(), dev = r.u64();
      std::uint8_t ssup = r.u8(), prov = r.u8();
      CreateBufferParams p;
      p.domain = domain8(dm);
      p.size = bsize;
      p.alignment = balign;
      p.device = DeviceId(dev);
      p.locality = Locality::LOCAL;
      p.support = support8(ssup);
      p.provenance = provenance8(prov);
      p.authority = auth;
      p.name = "worker_buffer";
      auto res = coord_.create_buffer(p);
      return res ? send_ack(conn, res->id.value(), res->generation.value())
                 : send_nack(conn, res.error().code, res.error().message);
    }
    case MessageType::REGISTER_BUFFER: {
      std::uint64_t bid = r.u64(), eid = r.u64();
      std::uint8_t ssup = r.u8(), prov = r.u8();
      RegisterParams p;
      p.buffer = BufferId(bid); p.endpoint = EndpointId(eid);
      p.authority = auth; p.support = support8(ssup); p.provenance = provenance8(prov);
      p.required_capabilities.clear();
      auto res = coord_.register_buffer(p, nullptr);
      return res ? send_ack(conn, res->id.value(), res->generation.value())
                 : send_nack(conn, res.error().code, res.error().message);
    }
    case MessageType::READY: {
      std::printf("WORKER_READY %llu\n", (unsigned long long)worker);
      std::fflush(stdout);
      return send_ack(conn);
    }
    default:
      return send_nack(conn, ErrorCode::UNSUPPORTED, "unsupported worker message");
  }
}

bool CoordinatorServer::handle_query(TcpConn& conn, const Frame& f) {
  (void)f;
  return send_state(conn);
}

bool CoordinatorServer::send_state(TcpConn& conn) {
  auto regs = coord_.registrations();
  auto workers = coord_.live_workers();
  Frame f;
  f.type = MessageType::ACK;
  pw_u8(f.payload, 0);
  pw_u32(f.payload, static_cast<std::uint32_t>(regs ? regs->size() : 0));
  if (regs) {
    for (const auto& r : *regs) {
      pw_u64(f.payload, r.id.value());
      pw_u64(f.payload, r.buffer.value());
      pw_u64(f.payload, r.endpoint.value());
      pw_u8(f.payload, static_cast<std::uint8_t>(static_cast<int>(r.state)));
      pw_u8(f.payload, static_cast<std::uint8_t>(r.authority.worker_owned() ? 1 : 0));
    }
  }
  pw_u32(f.payload, static_cast<std::uint32_t>(workers ? workers->size() : 0));
  if (workers) {
    for (const auto& [w, b] : *workers) {
      pw_u64(f.payload, w.value());
      pw_u64(f.payload, b.value());
    }
  }
  return conn.send_frame(f);
}

}  // namespace gpudirectfabric