#pragma once

#include "BraceletRuntimeStructs_c.h"
#include <string_view>

namespace bracelet_trace {

void traceBuffer(BraceletTraceSite *site, std::string_view value);
void traceWord(BraceletTraceSite *site, uintptr_t value);

} // namespace bracelet_trace
