#pragma once

#include <absl/strings/str_format.h>
#include <boost/outcome.hpp>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>

namespace bracelet {
struct ErrorInner;
struct Error {
  Error() : m_inner(nullptr) {}
  explicit Error(std::string_view msg);

  void add_context(std::string_view context);

  void print(std::ostream &os) const;
  friend std::ostream &operator<<(std::ostream &os, const Error &e) {
    e.print(os);
    return os;
  }

  bool empty() const { return m_inner == nullptr; }

private:
  struct Inner;
  struct DeleteInner {
    void operator()(Inner *) const noexcept;
  };
  std::unique_ptr<Inner, DeleteInner> m_inner;
};

template <typename T>
using Result = BOOST_OUTCOME_V2_NAMESPACE::result<
    T, Error, BOOST_OUTCOME_V2_NAMESPACE::policy::terminate>;
inline Result<void> ok() { return BOOST_OUTCOME_V2_NAMESPACE::success(); }

template <typename T> inline T unwrap(Result<T> &&r) {
  if (r) {
    if constexpr (!std::is_void_v<T>)
      return std::move(r.value());
  } else {
    std::cerr << "UNWRAP failed: " << r.error() << "\n";
    std::cerr.flush();
    abort();
  }
}

namespace result_detail {
struct AddContext {
  explicit AddContext(std::string context_msg) : context_msg(context_msg) {}
  // Error &&operator+(Error &&e) { abort(); }

  template <typename T> auto operator+(T &&e) {
    Error &e_inner = e.error();
    e_inner.add_context(context_msg);
    // Boost.Outcome passes a named, move-only failure object here.
    return std::move(e); // NOLINT(bugprone-move-forwarding-reference)
  }

private:
  std::string context_msg;
};
} // namespace result_detail
} // namespace bracelet

BOOST_OUTCOME_V2_NAMESPACE_BEGIN
inline bool try_operation_has_value(const boost::system::error_code &e) {
  return !e;
}
inline auto try_operation_return_as(const boost::system::error_code &e) {
  std::stringstream ss;
  ss << "boost system error: " << e;
  return failure(bracelet::Error(ss.str()));
}
inline void try_operation_extract_value(const boost::system::error_code &) {}
BOOST_OUTCOME_V2_NAMESPACE_END

// If the argument is an error, return it
#define BRACELET_TRY(...) BOOST_OUTCOME_TRYX(__VA_ARGS__)
// If the argument is an error, return it, adding an additional context string
// (formatted with absl::StrFormat)
#define BRACELET_TRY_CONTEXT(e, ...)                                             \
  BOOST_OUTCOME_TRYX2(BOOST_OUTCOME_TRY_UNIQUE_NAME,                           \
                      return ::bracelet::result_detail::AddContext(              \
                                 absl::StrFormat(__VA_ARGS__)) +               \
                      , e)
// If cond is false, return an error where the message are the
// absl::StrFormat()-ed arguments
#define BRACELET_ENSURE(cond, ...)                                               \
  if (__builtin_expect(!(cond), false))                                        \
  return ::bracelet::Error(::absl::StrFormat(__VA_ARGS__))
