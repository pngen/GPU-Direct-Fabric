#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

namespace gpudirectfabric {

// A monotonic, type-tagged generation. Generations never decrease in the runtime
// model; only next() produces a successor. The initial generation is 0 and is a
// valid (empty) starting point for a freshly created entity.
template <typename Tag>
class Generation {
 public:
  using value_type = std::uint64_t;

  constexpr Generation() noexcept = default;
  constexpr explicit Generation(value_type v) noexcept : v_(v) {}

  [[nodiscard]] constexpr value_type value() const noexcept { return v_; }
  [[nodiscard]] constexpr Generation next() const noexcept { return Generation(v_ + 1); }
  [[nodiscard]] constexpr Generation previous() const noexcept {
    return Generation(v_ == 0 ? 0 : v_ - 1);
  }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return v_ == 0; }

  explicit constexpr operator bool() const noexcept { return true; }

  friend constexpr bool operator==(const Generation& a, const Generation& b) noexcept = default;
  friend constexpr auto operator<=>(const Generation& a, const Generation& b) noexcept = default;

  [[nodiscard]] std::string str() const { return std::to_string(v_); }

 private:
  value_type v_ = 0;
};

// A strongly typed, non-interchangeable identity. A raw pointer or integer is
// never a durable identity; only these value types are. value 0 means "none".
template <typename Tag>
class Id {
 public:
  using value_type = std::uint64_t;

  constexpr Id() noexcept = default;
  constexpr explicit Id(value_type v) noexcept : v_(v) {}

  [[nodiscard]] constexpr value_type value() const noexcept { return v_; }
  [[nodiscard]] constexpr bool nil() const noexcept { return v_ == 0; }
  [[nodiscard]] constexpr bool valid() const noexcept { return v_ != 0; }

  explicit constexpr operator bool() const noexcept { return v_ != 0; }

  friend constexpr bool operator==(const Id& a, const Id& b) noexcept = default;
  friend constexpr auto operator<=>(const Id& a, const Id& b) noexcept = default;

  [[nodiscard]] std::string str() const { return std::to_string(v_); }

 private:
  value_type v_ = 0;
};

// ---- Identity tag types. Each produces a distinct, non-interchangeable type. ----

struct BufferIdTag {};
struct RegistrationIdTag {};
struct DeviceIdTag {};
struct EndpointIdTag {};
struct NicIdTag {};
struct StorageEndpointIdTag {};
struct PathIdTag {};
struct TransferPlanIdTag {};
struct TransferAttemptIdTag {};
struct VerificationIdTag {};
struct WorkerIdTag {};
struct CapabilityObservationIdTag {};
struct PolicyIdTag {};
struct ProviderIdTag {};

// ---- Generation tag types. ----

struct BufferGenerationTag {};
struct RegistrationGenerationTag {};
struct DeviceGenerationTag {};
struct EndpointGenerationTag {};
struct NicGenerationTag {};
struct StorageGenerationTag {};
struct TransferGenerationTag {};
struct PathGenerationTag {};
struct WorkerBootIdTag {};   // increments per boot of a given worker incarnation
struct CoordinatorEpochTag {};  // increments per coordinator (process) incarnation

using BufferId = Id<BufferIdTag>;
using RegistrationId = Id<RegistrationIdTag>;
using DeviceId = Id<DeviceIdTag>;
using EndpointId = Id<EndpointIdTag>;
using NicId = Id<NicIdTag>;
using StorageEndpointId = Id<StorageEndpointIdTag>;
using PathId = Id<PathIdTag>;
using TransferPlanId = Id<TransferPlanIdTag>;
using TransferAttemptId = Id<TransferAttemptIdTag>;
using VerificationId = Id<VerificationIdTag>;
using WorkerId = Id<WorkerIdTag>;
using CapabilityObservationId = Id<CapabilityObservationIdTag>;
using PolicyId = Id<PolicyIdTag>;
using ProviderId = Id<ProviderIdTag>;

using BufferGeneration = Generation<BufferGenerationTag>;
using RegistrationGeneration = Generation<RegistrationGenerationTag>;
using DeviceGeneration = Generation<DeviceGenerationTag>;
using EndpointGeneration = Generation<EndpointGenerationTag>;
using NicGeneration = Generation<NicGenerationTag>;
using StorageGeneration = Generation<StorageGenerationTag>;
using TransferGeneration = Generation<TransferGenerationTag>;
using PathGeneration = Generation<PathGenerationTag>;
using WorkerBootId = Generation<WorkerBootIdTag>;
using CoordinatorEpoch = Generation<CoordinatorEpochTag>;

// ---- Hash support for unordered containers. ----

template <typename Tag>
struct std::hash<::gpudirectfabric::Id<Tag>> {
  std::size_t operator()(const ::gpudirectfabric::Id<Tag>& id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value());
  }
};

template <typename Tag>
struct std::hash<::gpudirectfabric::Generation<Tag>> {
  std::size_t operator()(const ::gpudirectfabric::Generation<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};

}  // namespace gpudirectfabric
