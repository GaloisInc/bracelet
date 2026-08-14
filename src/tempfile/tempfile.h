#pragma once

#include "Result/Result.h"
#include <filesystem>
#include <optional>

namespace bracelet {
namespace tempfile {
struct TemporaryDirectory {
  // If specified, make the temporary directory inside the given parent
  static Result<TemporaryDirectory>
  create(const std::filesystem::path *tmpdir = nullptr);
  ~TemporaryDirectory();
  TemporaryDirectory(TemporaryDirectory &&) = default;
  TemporaryDirectory &operator=(TemporaryDirectory &&) = default;
  TemporaryDirectory(const TemporaryDirectory &) = delete;
  TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;

  const std::filesystem::path &path() const {
    assert(m_path);
    return *m_path;
  }

private:
  TemporaryDirectory();
  std::optional<std::filesystem::path> m_path;
};

} // namespace tempfile
} // namespace bracelet
