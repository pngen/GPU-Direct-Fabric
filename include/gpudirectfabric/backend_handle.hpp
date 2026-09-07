#pragma once

#include <cstdint>
#include <string>

namespace gpudirectfabric {

// An opaque, backend-specific handle. Never persisted and never treated as a
// durable identity; it is transient process-local evidence.
class BackendHandle {
 public:
  virtual ~BackendHandle() = default;
  virtual std::uint64_t opaque_id() const noexcept = 0;
  virtual std::string type_name() const = 0;
  virtual std::string describe() const = 0;
};

}  // namespace gpudirectfabric
