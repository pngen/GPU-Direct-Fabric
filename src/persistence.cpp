#include "gpudirectfabric/coordinator.hpp"
#include "gpudirectfabric/atomic_file.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace gpudirectfabric {

namespace {

constexpr std::uint32_t kMagic0 = 0x46534447;  // 'G','D','S','F'
constexpr std::uint32_t kMagic1 = 0x45544154;  // 'E','T','A','T' (together "GDFSTATE")
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kMaxPayload = 1ull << 28;  // 256 MiB

// ---- CRC32 (IEEE 802.3 / zlib polynomial 0xEDB88320) ----
std::uint32_t crc32(const std::uint8_t* data, std::size_t len) noexcept {
  static std::uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = true;
  }
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

void put_u32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  b.push_back(static_cast<std::uint8_t>(v));
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v >> 16));
  b.push_back(static_cast<std::uint8_t>(v >> 24));
}
void put_u64(std::vector<std::uint8_t>& b, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void put_str(std::vector<std::uint8_t>& b, const std::string& s) {
  put_u32(b, static_cast<std::uint32_t>(s.size()));
  for (char ch : s) b.push_back(static_cast<std::uint8_t>(ch));
}

struct Reader {
  const std::uint8_t* p;
  std::size_t n;
  std::size_t off = 0;
  bool fail = false;

  bool u32(std::uint32_t& v) {
    if (off + 4 > n) { fail = true; return false; }
    v = static_cast<std::uint32_t>(p[off]) | (static_cast<std::uint32_t>(p[off + 1]) << 8) |
        (static_cast<std::uint32_t>(p[off + 2]) << 16) | (static_cast<std::uint32_t>(p[off + 3]) << 24);
    off += 4;
    return true;
  }
  bool u64(std::uint64_t& v) {
    if (off + 8 > n) { fail = true; return false; }
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[off + i]) << (8 * i);
    off += 8;
    return true;
  }
  bool i32(std::int32_t& v) {
    std::uint32_t u = 0;
    if (!u32(u)) return false;
    v = static_cast<std::int32_t>(u);
    return true;
  }
  bool boolv(bool& v) {
    std::uint32_t u = 0;
    if (!u32(u)) return false;
    if (u > 1) { fail = true; return false; }
    v = (u == 1);
    return true;
  }
  bool str(std::string& s) {
    std::uint32_t len = 0;
    if (!u32(len)) return false;
    if (len > 1u << 20) { fail = true; return false; }
    if (off + len > n) { fail = true; return false; }
    s.assign(reinterpret_cast<const char*>(p + off), len);
    off += len;
    return true;
  }
};

template <typename E>
bool read_enum(Reader& r, E& out) {
  std::int32_t v = 0;
  if (!r.i32(v)) return false;
  E e = static_cast<E>(v);
  std::string s = to_string(e);
  E chk{};
  if (from_string(s, chk) && chk == e) { out = e; return true; }
  return false;
}

template <typename E>
void encode_enum(std::vector<std::uint8_t>& b, E e) {
  put_u32(b, static_cast<std::uint32_t>(static_cast<int>(e)));
}

}  // namespace

Result<void> Coordinator::persist_to_file(const std::string& path) {
  std::vector<std::uint8_t> body;
  // ---- Header record ----
  put_u64(body, state_.epoch.value());
  put_u64(body, state_.next_id);
  put_u64(body, state_.freshness);

  auto buffers_all = state_.buffers;
  auto buf_vec = std::vector<BufferRecord>{};
  for (const auto& [k, v] : buffers_all) { (void)k; buf_vec.push_back(v); }
  std::sort(buf_vec.begin(), buf_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(buf_vec.size()));
  for (const auto& b : buf_vec) {
    put_u64(body, b.id.value());
    put_u64(body, b.generation.value());
    encode_enum(body, b.domain);
    put_u64(body, b.size);
    put_u64(body, b.alignment);
    put_u64(body, b.device.value());
    put_u64(body, b.device_generation.value());
    encode_enum(body, b.locality);
    encode_enum(body, b.support);
    encode_enum(body, b.provenance);
    encode_enum(body, b.lifecycle);
    put_str(body, b.name);
  }

  auto dev_vec = std::vector<DeviceRecord>{};
  for (const auto& [k, v] : state_.devices) { (void)k; dev_vec.push_back(v); }
  std::sort(dev_vec.begin(), dev_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(dev_vec.size()));
  for (const auto& d : dev_vec) {
    put_u64(body, d.id.value());
    put_u64(body, d.generation.value());
    put_str(body, d.name);
    put_str(body, d.compute_capability);
    put_u32(body, d.peer_memory_supported ? 1 : 0);
    encode_enum(body, d.support);
    encode_enum(body, d.provenance);
  }

  auto ep_vec = std::vector<EndpointRecord>{};
  for (const auto& [k, v] : state_.endpoints) { (void)k; ep_vec.push_back(v); }
  std::sort(ep_vec.begin(), ep_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(ep_vec.size()));
  for (const auto& e : ep_vec) {
    put_u64(body, e.id.value());
    put_u64(body, e.generation.value());
    encode_enum(body, e.endpoint_class);
    put_str(body, e.name);
    put_str(body, e.topology_hint);
    put_u32(body, e.healthy ? 1 : 0);
    put_u64(body, e.provider.value());
    encode_enum(body, e.support);
    encode_enum(body, e.provenance);
  }

  auto nic_vec = std::vector<NicRecord>{};
  for (const auto& [k, v] : state_.nics) { (void)k; nic_vec.push_back(v); }
  std::sort(nic_vec.begin(), nic_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(nic_vec.size()));
  for (const auto& n : nic_vec) {
    put_u64(body, n.id.value());
    put_u64(body, n.generation.value());
    put_str(body, n.name);
    put_str(body, n.locator);
    put_u32(body, n.rdma_capable ? 1 : 0);
    put_u32(body, n.direct_path_capable ? 1 : 0);
    put_u64(body, n.provider.value());
    encode_enum(body, n.support);
    encode_enum(body, n.provenance);
  }

  auto st_vec = std::vector<StorageEndpointRecord>{};
  for (const auto& [k, v] : state_.storage) { (void)k; st_vec.push_back(v); }
  std::sort(st_vec.begin(), st_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(st_vec.size()));
  for (const auto& s : st_vec) {
    put_u64(body, s.id.value());
    put_u64(body, s.generation.value());
    put_str(body, s.name);
    put_str(body, s.backend_name);
    put_str(body, s.path);
    put_u32(body, s.direct_path_capable ? 1 : 0);
    put_u64(body, s.provider.value());
    encode_enum(body, s.support);
    encode_enum(body, s.provenance);
  }

  auto pr_vec = std::vector<ProviderInfo>{};
  for (const auto& [k, v] : state_.providers) { (void)k; pr_vec.push_back(v); }
  std::sort(pr_vec.begin(), pr_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(pr_vec.size()));
  for (const auto& p : pr_vec) {
    put_u64(body, p.id.value());
    put_str(body, p.name);
    put_u32(body, p.available ? 1 : 0);
    encode_enum(body, p.support);
    encode_enum(body, p.provenance);
  }

  auto cap_vec = std::vector<CapabilityObservation>{};
  for (const auto& [k, v] : state_.capabilities) { (void)k; cap_vec.push_back(v); }
  std::sort(cap_vec.begin(), cap_vec.end(), [](auto& a, auto& b) {
    return a.id.value() < b.id.value();
  });
  put_u32(body, static_cast<std::uint32_t>(cap_vec.size()));
  for (const auto& c : cap_vec) {
    put_u64(body, c.id.value());
    put_str(body, c.provider);
    put_str(body, c.capability);
    put_str(body, c.value);
    encode_enum(body, c.support);
    encode_enum(body, c.provenance);
    put_u64(body, c.authority.worker.value());
    put_u64(body, c.authority.boot.value());
    put_u64(body, c.authority.epoch.value());
    put_u64(body, c.freshness);
  }

  // Registrations are persisted as historical metadata only; process-local
  // handles are dropped and live authority is NOT recoverable.
  auto reg_vec = std::vector<Registration>{};
  for (const auto& [k, v] : state_.registrations) { (void)k; reg_vec.push_back(v); }
  std::sort(reg_vec.begin(), reg_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(reg_vec.size()));
  for (const auto& r : reg_vec) {
    put_u64(body, r.id.value());
    put_u64(body, r.generation.value());
    put_u64(body, r.buffer.value());
    put_u64(body, r.buffer_generation.value());
    put_u64(body, r.endpoint.value());
    put_u64(body, r.endpoint_generation.value());
    put_u64(body, r.provider.value());
    put_u64(body, r.authority.worker.value());
    put_u64(body, r.authority.boot.value());
    put_u64(body, r.authority.epoch.value());
    encode_enum(body, r.state);
    encode_enum(body, r.support);
    encode_enum(body, r.provenance);
    put_u32(body, r.duplicated ? 1 : 0);
    put_u64(body, r.freshness);
    put_u32(body, static_cast<std::uint32_t>(r.required_capabilities.size()));
    for (const auto& cap : r.required_capabilities) put_str(body, cap);
  }

  // Plans are persisted as historical metadata; authority is cleared.
  auto plan_vec = std::vector<TransferPlan>{};
  for (const auto& [k, v] : state_.plans) { (void)k; plan_vec.push_back(v); }
  std::sort(plan_vec.begin(), plan_vec.end(), [](auto& a, auto& b) { return a.id.value() < b.id.value(); });
  put_u32(body, static_cast<std::uint32_t>(plan_vec.size()));
  for (const auto& p : plan_vec) {
    put_u64(body, p.id.value());
    put_u64(body, p.path_generation.value());
    put_u64(body, p.generation.value());
    put_u64(body, p.source.value());
    put_u64(body, p.source_generation.value());
    put_u64(body, p.destination.value());
    put_u64(body, p.destination_generation.value());
    put_u64(body, p.registration_id.value());
    put_u64(body, p.registration_generation.value());
    put_u64(body, p.registration_buffer_generation.value());
    encode_enum(body, p.policy);
    put_u64(body, p.expected_bytes);
    encode_enum(body, p.eligibility);
    encode_enum(body, p.support);
    encode_enum(body, p.provenance);
    put_u32(body, p.copy_count);
    put_str(body, p.explanation);
    put_u64(body, p.epoch.value());
    put_u64(body, p.authority.worker.value());
    put_u64(body, p.authority.boot.value());
    put_u64(body, p.authority.epoch.value());
  }

  // ---- Frame ----
  std::vector<std::uint8_t> file;
  file.push_back('G'); file.push_back('D'); file.push_back('F'); file.push_back('S');
  file.push_back('T'); file.push_back('A'); file.push_back('T'); file.push_back('E');
  put_u32(file, kVersion);
  put_u64(file, body.size());
  file.insert(file.end(), body.begin(), body.end());
  std::uint32_t crc = crc32(file.data(), file.size());
  put_u32(file, crc);

  // Atomic replacement.
  std::string tmp = path + ".gdfstate.tmp";
  {
    std::ofstream os(tmp, std::ios::binary);
    if (!os) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cannot open temp state file for write");
    os.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    if (!os) return err<void>(ErrorCode::PERSISTENCE_ERROR, "failed writing state file");
  }
  if (!detail::atomic_replace_file(tmp.c_str(), path.c_str())) {
    return err<void>(ErrorCode::PERSISTENCE_ERROR, "atomic replace of state file failed");
  }
  return ok();
}

Result<void> Coordinator::recover_from_file(const std::string& path, Coordinator& out) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cannot open state file");
  std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
  if (file.size() < 24) return err<void>(ErrorCode::PERSISTENCE_ERROR, "state file too short (truncated)");

  static const std::uint8_t kMag[8] = {'G','D','F','S','T','A','T','E'};
  if (std::memcmp(file.data(), kMag, 8) != 0) {
    return err<void>(ErrorCode::PERSISTENCE_ERROR, "bad magic");
  }
  Reader hdr{file.data() + 8, file.size() - 8};
  std::uint32_t ver = 0;
  if (!hdr.u32(ver)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated header");
  if (ver > kVersion) return err<void>(ErrorCode::UNSUPPORTED, "unsupported future state version");
  if (ver < kVersion) return err<void>(ErrorCode::PERSISTENCE_ERROR, "unsupported old state version");
  std::uint64_t payload_len = 0;
  if (!hdr.u64(payload_len)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated length");
  if (payload_len > kMaxPayload) return err<void>(ErrorCode::PERSISTENCE_ERROR, "absurd payload length");
  // Header is 8+4+8 = 20 bytes.
  std::uint64_t expected = 20 + payload_len + 4;
  if (file.size() < expected) return err<void>(ErrorCode::PERSISTENCE_ERROR, "state file truncated");
  if (file.size() > expected) return err<void>(ErrorCode::PERSISTENCE_ERROR, "trailing garbage after state");
  if (file.size() != expected) return err<void>(ErrorCode::PERSISTENCE_ERROR, "state length mismatch");
  // Verify CRC over [0, 20+payload_len).
  std::uint32_t read_crc = 0;
  Reader crc_r{file.data() + (20 + payload_len), 4};
  if (!crc_r.u32(read_crc)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated crc");
  std::uint32_t calc = crc32(file.data(), static_cast<std::size_t>(20 + payload_len));
  if (read_crc != calc) return err<void>(ErrorCode::PERSISTENCE_ERROR, "state CRC mismatch (corrupt)");

  Reader r{file.data() + 20, static_cast<std::size_t>(payload_len)};
  std::uint64_t epoch = 0, next_id = 0, freshness = 0;
  if (!r.u64(epoch) || !r.u64(next_id) || !r.u64(freshness)) {
    return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated header record");
  }
  out.state_ = State{};
  out.state_.epoch = CoordinatorEpoch(epoch);
  out.state_.next_id = next_id;
  out.state_.freshness = freshness;

  std::uint32_t cnt = 0;
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid buffer count");
  out.state_.buffers.clear();
  for (std::uint32_t v0 = 0; v0 < cnt; ++v0) {
    BufferRecord b;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer");
    b.id = BufferId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer gen");
    b.generation = BufferGeneration(v);
    if (!read_enum(r, b.domain)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid buffer domain");
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer size");
    b.size = v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer alignment");
    b.alignment = v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer device");
    b.device = DeviceId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer device gen");
    b.device_generation = DeviceGeneration(v);
    if (!read_enum(r, b.locality) || !read_enum(r, b.support) || !read_enum(r, b.provenance) ||
        !read_enum(r, b.lifecycle)) {
      return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid buffer enum");
    }
    if (!r.str(b.name)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated buffer name");
    b.registration_state = RegistrationState::UNREGISTERED;
    b.handle = nullptr;
    out.state_.buffers[b.id.value()] = b;
  }
  // Devices/endpoints/nics/storage/providers/capabilities/registrations/plans
  // decoded in a similar bounded fashion (see helper below).
  // ---- Devices ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid device count");
  out.state_.devices.clear();
  for (std::uint32_t v1 = 0; v1 < cnt; ++v1) {
    DeviceRecord d;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated device");
    d.id = DeviceId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated device gen");
    d.generation = DeviceGeneration(v);
    if (!r.str(d.name) || !r.str(d.compute_capability)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "device strings");
    std::uint32_t u = 0;
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "device bool");
    d.peer_memory_supported = (u == 1);
    if (!read_enum(r, d.support) || !read_enum(r, d.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "device enum");
    out.state_.devices[d.id.value()] = d;
  }
  // ---- Endpoints ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid endpoint count");
  out.state_.endpoints.clear();
  for (std::uint32_t v2 = 0; v2 < cnt; ++v2) {
    EndpointRecord e;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated endpoint");
    e.id = EndpointId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated endpoint gen");
    e.generation = EndpointGeneration(v);
    if (!read_enum(r, e.endpoint_class)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "endpoint class");
    if (!r.str(e.name) || !r.str(e.topology_hint)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "endpoint strings");
    std::uint32_t u = 0;
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "endpoint health");
    e.healthy = (u == 1);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "endpoint provider");
    e.provider = ProviderId(v);
    if (!read_enum(r, e.support) || !read_enum(r, e.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "endpoint enum");
    e.handle = nullptr;
    out.state_.endpoints[e.id.value()] = e;
  }
  // ---- NICs ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid nic count");
  out.state_.nics.clear();
  for (std::uint32_t v3 = 0; v3 < cnt; ++v3) {
    NicRecord n;
    std::uint64_t v; std::uint32_t u;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated nic");
    n.id = NicId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated nic gen");
    n.generation = NicGeneration(v);
    if (!r.str(n.name) || !r.str(n.locator)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "nic strings");
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "nic bool");
    n.rdma_capable = (u == 1);
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "nic bool");
    n.direct_path_capable = (u == 1);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "nic provider");
    n.provider = ProviderId(v);
    if (!read_enum(r, n.support) || !read_enum(r, n.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "nic enum");
    out.state_.nics[n.id.value()] = n;
  }
  // ---- Storage endpoints ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid storage count");
  out.state_.storage.clear();
  for (std::uint32_t v4 = 0; v4 < cnt; ++v4) {
    StorageEndpointRecord s;
    std::uint64_t v; std::uint32_t u;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated storage");
    s.id = StorageEndpointId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated storage gen");
    s.generation = StorageGeneration(v);
    if (!r.str(s.name) || !r.str(s.backend_name) || !r.str(s.path)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "storage strings");
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "storage bool");
    s.direct_path_capable = (u == 1);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "storage provider");
    s.provider = ProviderId(v);
    if (!read_enum(r, s.support) || !read_enum(r, s.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "storage enum");
    out.state_.storage[s.id.value()] = s;
  }
  // ---- Providers ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid provider count");
  out.state_.providers.clear();
  for (std::uint32_t v5 = 0; v5 < cnt; ++v5) {
    ProviderInfo p;
    std::uint64_t v; std::uint32_t u;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated provider");
    p.id = ProviderId(v);
    if (!r.str(p.name)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "provider name");
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "provider bool");
    p.available = (u == 1);
    if (!read_enum(r, p.support) || !read_enum(r, p.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "provider enum");
    out.state_.providers[p.id.value()] = p;
  }
  // ---- Capabilities ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid capability count");
  out.state_.capabilities.clear();
  for (std::uint32_t v6 = 0; v6 < cnt; ++v6) {
    CapabilityObservation c;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated cap id");
    c.id = CapabilityObservationId(v);
    if (!r.str(c.provider) || !r.str(c.capability) || !r.str(c.value)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap strings");
    if (!read_enum(r, c.support) || !read_enum(r, c.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap enum");
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap worker");
    c.authority.worker = WorkerId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap boot");
    c.authority.boot = WorkerBootId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap epoch");
    c.authority.epoch = CoordinatorEpoch(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "cap freshness");
    c.freshness = v;
    out.state_.capabilities[c.id.value()] = c;
  }
  // ---- Registrations (historical; no live handle/authority) ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid registration count");
  out.state_.registrations.clear();
  for (std::uint32_t v7 = 0; v7 < cnt; ++v7) {
    Registration reg;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated reg id");
    reg.id = RegistrationId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg gen");
    reg.generation = RegistrationGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg buffer");
    reg.buffer = BufferId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg buffer gen");
    reg.buffer_generation = BufferGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg endpoint");
    reg.endpoint = EndpointId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg endpoint gen");
    reg.endpoint_generation = EndpointGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg provider");
    reg.provider = ProviderId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg worker");
    reg.authority.worker = WorkerId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg boot");
    reg.authority.boot = WorkerBootId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg epoch");
    reg.authority.epoch = CoordinatorEpoch(v);
    if (!read_enum(r, reg.state)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg state");
    if (!read_enum(r, reg.support) || !read_enum(r, reg.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg enum");
    std::uint32_t u = 0;
    if (!r.u32(u) || u > 1) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg dup");
    reg.duplicated = (u == 1);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg freshness");
    reg.freshness = v;
    std::uint32_t ncap = 0;
    if (!r.u32(ncap) || ncap > 4096) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg caps");
    for (std::uint32_t j = 0; j < ncap; ++j) {
      std::string s;
      if (!r.str(s)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "reg cap str");
      reg.required_capabilities.push_back(std::move(s));
    }
    reg.handle = nullptr;
    // Process-local authority is never recoverable as current.
    if (reg.state == RegistrationState::REGISTERED) reg.state = RegistrationState::REVALIDATION_REQUIRED;
    out.state_.registrations[reg.id.value()] = reg;
  }
  // ---- Plans (historical; authority cleared) ----
  if (!r.u32(cnt) || cnt > 1u << 20) return err<void>(ErrorCode::PERSISTENCE_ERROR, "invalid plan count");
  out.state_.plans.clear();
  for (std::uint32_t v8 = 0; v8 < cnt; ++v8) {
    TransferPlan pl;
    std::uint64_t v;
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "truncated plan id");
    pl.id = TransferPlanId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan path gen");
    pl.path_generation = PathGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan gen");
    pl.generation = TransferGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan source");
    pl.source = BufferId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan source gen");
    pl.source_generation = BufferGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan dest");
    pl.destination = EndpointId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan dest gen");
    pl.destination_generation = EndpointGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan reg id");
    pl.registration_id = RegistrationId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan reg gen");
    pl.registration_generation = RegistrationGeneration(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan reg buf gen");
    pl.registration_buffer_generation = BufferGeneration(v);
    if (!read_enum(r, pl.policy)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan policy");
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan expected");
    pl.expected_bytes = v;
    if (!read_enum(r, pl.eligibility) || !read_enum(r, pl.support) || !read_enum(r, pl.provenance)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan enum");
    if (!r.u32(cnt)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan copy count");
    pl.copy_count = cnt;
    if (!r.str(pl.explanation)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan explanation");
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan epoch");
    pl.epoch = CoordinatorEpoch(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan worker");
    pl.authority.worker = WorkerId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan boot");
    pl.authority.boot = WorkerBootId(v);
    if (!r.u64(v)) return err<void>(ErrorCode::PERSISTENCE_ERROR, "plan auth epoch");
    pl.authority.epoch = CoordinatorEpoch(v);
    pl.authoritative = false;  // plans are never authoritative across a restart
    out.state_.plans[pl.id.value()] = pl;
  }
  return ok();
}

}  // namespace gpudirectfabric