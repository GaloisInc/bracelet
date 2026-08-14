#include "tempfile.h"
#include <absl/log/log.h>
#include <filesystem>
#include <string.h>

using namespace bracelet;
using namespace bracelet::tempfile;

Result<TemporaryDirectory>
TemporaryDirectory::create(const std::filesystem::path *tmpdir) {
  std::error_code err;
  std::filesystem::path tmp;
  if (tmpdir)
    tmp = *tmpdir;
  else {
    tmp = std::filesystem::temp_directory_path(err);
    BRACELET_TRY_CONTEXT(err, "can't find temporary directory storage");
  }
  tmp /= "tmpdir-XXXXXX";
  std::string tmp_path(tmp);
  BRACELET_ENSURE(mkdtemp(tmp_path.data()) != nullptr,
                "Cannot make temporary directory %v: %s", tmp, strerror(errno));
  TemporaryDirectory out;
  out.m_path = tmp_path;
  return out;
}
TemporaryDirectory::TemporaryDirectory() {}
TemporaryDirectory::~TemporaryDirectory() {
  std::optional<std::filesystem::path> p;
  std::swap(p, m_path);
  if (!p)
    return;
  std::error_code err;
  std::filesystem::remove_all(*p, err);
  if (err) {
    LOG(WARNING) << "Couldn't clean-up temporary directory " << p->c_str()
                 << "due to: " << err;
  }
}
