#pragma once

#include "gpudirectfabric/enums.hpp"

#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace gpudirectfabric {

// A structured error carrying a typed error code plus human readable context.
struct Error {
  ErrorCode code = ErrorCode::OK;
  std::string message;
  std::string detail;

  [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::OK; }
  [[nodiscard]] explicit operator bool() const noexcept { return code != ErrorCode::OK; }
};

// Factory helpers.
[[nodiscard]] inline Error make_error(ErrorCode code, std::string message = {}, std::string detail = {}) {
  Error e;
  e.code = code;
  e.message = std::move(message);
  e.detail = std::move(detail);
  return e;
}

namespace detail {

template <typename T>
class ResultBase {
 public:
  ResultBase(Error err) : value_(std::move(err)) {}
  ResultBase(const T& value) : value_(value) {}
  ResultBase(T&& value) : value_(std::move(value)) {}
  ResultBase(const ResultBase&) = default;
  ResultBase(ResultBase&&) noexcept = default;
  ResultBase& operator=(const ResultBase&) = default;
  ResultBase& operator=(ResultBase&&) noexcept = default;

  [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(value_); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const Error& error() const {
    if (has_value()) throw std::bad_variant_access();
    return std::get<Error>(value_);
  }
  [[nodiscard]] Error& error() {
    if (has_value()) throw std::bad_variant_access();
    return std::get<Error>(value_);
  }
  [[nodiscard]] T& value() & {
    if (!has_value()) throw std::bad_variant_access();
    return std::get<T>(value_);
  }
  [[nodiscard]] const T& value() const& {
    if (!has_value()) throw std::bad_variant_access();
    return std::get<T>(value_);
  }
  [[nodiscard]] T&& value() && {
    if (!has_value()) throw std::bad_variant_access();
    return std::move(std::get<T>(value_));
  }
  [[nodiscard]] const T& operator*() const& { return value(); }
  [[nodiscard]] T& operator*() & { return value(); }
  [[nodiscard]] const T* operator->() const { return &value(); }
  [[nodiscard]] T* operator->() { return &value(); }
  [[nodiscard]] T value_or(T&& dflt) const& {
    return has_value() ? std::get<T>(value_) : std::move(dflt);
  }

 private:
  std::variant<T, Error> value_;
};

template <>
class ResultBase<void> {
 public:
  ResultBase(Error err) : value_(std::move(err)) {}
  ResultBase() : value_(std::monostate{}) {}
  [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<std::monostate>(value_); }
  [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] const Error& error() const {
    if (has_value()) throw std::bad_variant_access();
    return std::get<Error>(value_);
  }
  [[nodiscard]] Error& error() {
    if (has_value()) throw std::bad_variant_access();
    return std::get<Error>(value_);
  }
  void value() const { if (!has_value()) throw std::bad_variant_access(); }

 private:
  std::variant<std::monostate, Error> value_;
};

}  // namespace detail

template <typename T>
class Result : public detail::ResultBase<T> {
 public:
  using detail::ResultBase<T>::ResultBase;
};

// Result<void> construction from an error: Result<void>(Error{}).
template <>
class Result<void> : public detail::ResultBase<void> {
 public:
  using detail::ResultBase<void>::ResultBase;
};

// Convenience factories.
template <typename T>
[[nodiscard]] Result<T> ok(T value) {
  return Result<T>(std::move(value));
}

[[nodiscard]] inline Result<void> ok() { return Result<void>(); }

[[nodiscard]] inline Result<void> err(Error e) { return Result<void>(std::move(e)); }

template <typename T>
[[nodiscard]] Result<T> err(Error e) {
  return Result<T>(std::move(e));
}

template <typename T>
[[nodiscard]] Result<T> err(ErrorCode code, std::string message = {}, std::string detail = {}) {
  return Result<T>(make_error(code, std::move(message), std::move(detail)));
}

}  // namespace gpudirectfabric
