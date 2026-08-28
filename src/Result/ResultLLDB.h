// Support BRACELET_TRY() et al with lldb Errors
#pragma once

#include "Result.h"

#include <lldb/API/SBError.h>

BOOST_OUTCOME_V2_NAMESPACE_BEGIN
inline bool try_operation_has_value(const lldb::SBError &e) {
  return e.Success();
}
inline auto try_operation_return_as(lldb::SBError &&e) {
  std::string msg = "LLDB error: ";
  msg += e.GetCString();
  return failure(bracelet::Error(std::move(msg)));
}
inline void try_operation_extract_value(lldb::SBError &&) {}
BOOST_OUTCOME_V2_NAMESPACE_END
