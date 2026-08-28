// Support BRACELET_TRY() et al with llvm Errors
#pragma once

#include "Result.h"

#include <llvm/Support/Error.h>

BOOST_OUTCOME_V2_NAMESPACE_BEGIN
inline bool try_operation_has_value(llvm::Error &e) { return !e; }
inline auto try_operation_return_as(llvm::Error &&e) {
  return failure(bracelet::Error(llvm::toString(std::move(e))));
}
inline void try_operation_extract_value(llvm::Error &&) {}

template <class T> inline bool try_operation_has_value(llvm::Expected<T> &v) {
  // GCC doesn't like it if we try to use the implicit conversion.
  return v ? true : false;
}
template <class T> inline auto try_operation_return_as(llvm::Expected<T> &&v) {
  llvm::Error e = v.takeError();
  return try_operation_return_as(std::move(e));
}
template <class T>
inline auto try_operation_extract_value(llvm::Expected<T> &&v) {
  return std::move(*v);
}
BOOST_OUTCOME_V2_NAMESPACE_END
