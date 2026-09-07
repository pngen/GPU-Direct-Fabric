#include "gpudirectfabric/backend.hpp"

#include <string>

namespace gpudirectfabric {

void BackendRegistry::add(std::shared_ptr<Backend> backend) {
  if (backend) backends_.push_back(std::move(backend));
}

const Backend* BackendRegistry::find_by_name(const std::string& name) const noexcept {
  for (const auto& b : backends_) {
    if (b->name() == name) return b.get();
  }
  return nullptr;
}

std::vector<std::string> BackendRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(backends_.size());
  for (const auto& b : backends_) out.push_back(b->name());
  return out;
}

}  // namespace gpudirectfabric
