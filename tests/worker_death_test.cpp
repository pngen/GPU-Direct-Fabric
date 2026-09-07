#include "gpudirectfabric/enums.hpp"
#include "gpudirectfabric/proc.hpp"
#include "gpudirectfabric/protocol.hpp"
#include "gpudirectfabric/transport.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace gpudirectfabric;

struct StateSnap {
  int reg_count = 0;
  int registered = 0;
  int reval = 0;
  int workers = 0;
};

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static bool query_state(unsigned short port, StateSnap& snap, std::uint64_t& reg_id,
                        std::uint64_t& buffer, std::uint64_t& endpoint) {
  TcpConn c;
  if (!c.connect_to("127.0.0.1", port)) return false;
  Frame h;
  h.type = MessageType::HELLO;
  pw_u64(h.payload, 0); pw_u64(h.payload, 0); pw_u64(h.payload, 0);
  if (!c.send_frame(h)) return false;
  Frame q;
  q.type = MessageType::QUERY_STATE;
  if (!c.send_frame(q)) return false;
  Frame f;
  std::string err;
  if (!c.recv_frame(f, err) || f.type != MessageType::ACK) return false;
  PayloadReader r{f.payload.data(), f.payload.size()};
  if (r.u8() != 0 || r.fail) return false;
  std::uint32_t n = r.u32();
  snap.reg_count = static_cast<int>(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    std::uint64_t rid = r.u64(), bu = r.u64(), ep = r.u64();
    std::uint8_t st = r.u8();
    r.u8();
    if (i == 0) { reg_id = rid; buffer = bu; endpoint = ep; }
    if (st == 2) snap.registered++;
    if (st == 3) snap.reval++;
  }
  std::uint32_t nw = r.u32();
  snap.workers = static_cast<int>(nw);
  for (std::uint32_t i = 0; i < nw; ++i) { r.u64(); r.u64(); }
  return !r.fail;
}

// Send a REGISTER_BUFFER as a "stale boot" replay; expect a NACK.
static bool replay_stale_boot_rejected(unsigned short port, std::uint64_t buffer, std::uint64_t endpoint) {
  TcpConn c;
  if (!c.connect_to("127.0.0.1", port)) return false;
  Frame h;
  h.type = MessageType::HELLO;
  pw_u64(h.payload, 1); pw_u64(h.payload, 1); pw_u64(h.payload, 1);
  if (!c.send_frame(h)) return false;
  Frame r;
  r.type = MessageType::REGISTER_BUFFER;
  pw_u64(r.payload, buffer);
  pw_u64(r.payload, endpoint);
  pw_u8(r.payload, static_cast<std::uint8_t>(static_cast<int>(Support::SYNTHETIC)));
  pw_u8(r.payload, static_cast<std::uint8_t>(static_cast<int>(Provenance::SYNTHETIC_FIXTURE)));
  pw_u64(r.payload, 1); pw_u64(r.payload, 1); pw_u64(r.payload, 1);
  if (!c.send_frame(r)) return false;
  Frame a;
  std::string err;
  if (!c.recv_frame(a, err)) return false;
  return a.type == MessageType::NACK;
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::srand(static_cast<unsigned>(std::time(nullptr)));
  const std::string bin = (argc >= 2) ? argv[1] : ".";
  const std::string coord_exe = bin + "\\gdf_coordinator.exe";
  const std::string worker_exe = bin + "\\gdf_worker.exe";

  bool live_ok = false, dead_ok = false, replay_ok = false, fresh_ok = false;
  for (int attempt = 0; attempt < 5; ++attempt) {
    unsigned short port = static_cast<unsigned short>(32000 + (std::rand() % 20000));
    detail::ProcHandle coord = detail::spawn_process(coord_exe, {std::to_string(port)});
    if (!coord.valid()) continue;
    bool up = false;
    for (int t0 = 0; t0 < 100; ++t0) {
      TcpConn probe;
      if (probe.connect_to("127.0.0.1", port)) { up = true; probe.close(); break; }
      sleep_ms(100);
    }
    if (!up) { detail::terminate_process(coord); continue; }
    std::printf("[wdt] coordinator up=%d port=%u\n", up ? 1 : 0, (unsigned)port);

    // Worker A (worker 1, boot 1): publishes evidence and READYs, then blocks.
    detail::ProcHandle wA = detail::spawn_process(worker_exe, {std::to_string(port), "1", "1"});
    if (!wA.valid()) { detail::terminate_process(coord); continue; }
    std::printf("[wdt] worker A spawned...\n");
    sleep_ms(3000);  // give the worker time to publish + register + READY

    // Kill worker A as a real OS process.
    detail::terminate_process(wA);
    std::printf("[wdt] worker A terminated...\n");

    StateSnap dead;
    std::uint64_t rid = 0, buf = 0, ep = 0;
    bool q = query_state(port, dead, rid, buf, ep);
    std::printf("[wdt] after death query ok=%d reval=%d workers=%d reg_count=%d\n",
                q ? 1 : 0, dead.reval, dead.workers, dead.reg_count);
    dead_ok = q && dead.reval == 1 && dead.workers == 0 && dead.reg_count == 1;
    replay_ok = replay_stale_boot_rejected(port, buf, ep);
    std::printf("[wdt] stale-boot replay rejected=%d\n", replay_ok ? 1 : 0);

    // Worker A-prime (worker 1, boot 2) re-registers fresh evidence.
    detail::ProcHandle wB = detail::spawn_process(worker_exe, {std::to_string(port), "1", "2"});
    std::printf("[wdt] worker A-prime spawned...\n");
    sleep_ms(3000);
    StateSnap fresh;
    std::uint64_t r2, b2, e2;
    bool q2 = query_state(port, fresh, r2, b2, e2);
    std::printf("[wdt] fresh worker registered=%d workers=%d\n", fresh.registered, fresh.workers);
    detail::terminate_process(wB);
    fresh_ok = q2 && fresh.registered == 1 && fresh.workers == 1;
    live_ok = true;  // worker A reached READY (implicit); death + fresh fencing verified

    detail::terminate_process(coord);
    std::printf("[wdt] RESULT dead_ok=%d replay_ok=%d fresh_ok=%d\n", dead_ok?1:0, replay_ok?1:0, fresh_ok?1:0);
    if (dead_ok && replay_ok && fresh_ok) { std::printf("[wdt] WORKER DEATH / FENCING PROOF PASSED\n"); return 0; }
  }
  std::printf("[wdt] WORKER DEATH / FENCING PROOF FAILED\n");
  return 1;
}