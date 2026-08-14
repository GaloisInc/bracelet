#pragma once

#include "Result/Result.h"
#include "boost/core/span.hpp"
#include <filesystem>
#include <initializer_list>

namespace bracelet {
namespace subprocess {
Result<int> call(std::string_view cmd, boost::span<std::string_view> args,
                 const std::filesystem::path *cwd = nullptr);
inline Result<int> call(std::string_view cmd,
                        std::initializer_list<std::string_view> args,
                        const std::filesystem::path *cwd = nullptr) {
  std::vector<std::string_view> args_vec;
  for (const auto &arg : args)
    args_vec.push_back(arg);
  return call(cmd, boost::span(args_vec), cwd);
}
} // namespace subprocess
} // namespace bracelet
