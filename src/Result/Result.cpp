#ifdef BRACELET_HAS_LIBBACKTRACE
// libbacktrace gets us line numbers
#define BOOST_STACKTRACE_USE_BACKTRACE 1
#include <boost/stacktrace.hpp>
#else
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#endif

#include "Result.h"
#include "absl/strings/str_format.h"
#include <vector>

using namespace bracelet;

struct Error::Inner {
  explicit Inner(std::string_view input_msg) {
    message += input_msg;
#ifdef BRACELET_HAS_LIBBACKTRACE
    backtrace = boost::stacktrace::to_string(boost::stacktrace::stacktrace());
#else
    std::vector<void *> stack_trace(1024);
    auto trace_depth =
        absl::GetStackTrace(stack_trace.data(), stack_trace.size(), 1);
    stack_trace.resize(trace_depth);
    std::vector<char> symbol_name(1024);
    for (void *pc : stack_trace) {
      bool success =
          absl::Symbolize(pc, symbol_name.data(), symbol_name.size());
      absl::StrAppendFormat(&backtrace, "  %s\n",
                            success ? symbol_name.data() : "(unknown)");
    }
#endif
    absl::StrAppendFormat(&message, "\n\n");
  }

  std::string message;
  std::string backtrace;
};

Error::Error(std::string_view msg)
    : m_inner(new Error::Inner(msg), Error::DeleteInner{}) {}

void Error::print(std::ostream &os) const {
  assert(m_inner && "Empty/moved error");
  os << "ERROR: " << m_inner->message << "\nBACKTRACE:\n"
     << m_inner->backtrace << "\n";
}

void Error::add_context(std::string_view context) {
  assert(m_inner && "Empty/moved error");
  m_inner->message += "With context: ";
  m_inner->message += context;
  m_inner->message += "\n";
}

void Error::DeleteInner::operator()(Inner *i) const noexcept { delete i; }
