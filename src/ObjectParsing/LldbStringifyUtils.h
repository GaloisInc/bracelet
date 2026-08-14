// Let lldb values be formatted using absl

#pragma once

#include <absl/strings/str_format.h>
#include <lldb/API/SBModule.h>
#include <lldb/API/SBStream.h>
#include <string_view>

namespace lldb {
// NOLINTBEGIN(bugprone-macro-parentheses)
#define STRINGIFY(ty)                                                          \
  template <typename Sink> void AbslStringify(Sink &sink, const ty &value) {   \
    lldb::SBStream stream;                                                     \
    const_cast<ty *>(&value)->GetDescription(stream);                          \
    absl::Format(&sink, "%s",                                                  \
                 std::string_view(stream.GetData(), stream.GetSize()));        \
  }
STRINGIFY(SBModule)
STRINGIFY(SBAddress)
STRINGIFY(SBSymbol)
STRINGIFY(SBFunction)
STRINGIFY(SBSection)
STRINGIFY(SBCompileUnit)
#undef STRINGIFY
// NOLINTEND(bugprone-macro-parentheses)
} // namespace lldb
