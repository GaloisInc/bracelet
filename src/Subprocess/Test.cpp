#include "Subprocess.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("true") {
  REQUIRE(bracelet::unwrap(bracelet::subprocess::call(
              "true", boost::span<std::string_view>())) == 0);
}
TEST_CASE("false") {
  REQUIRE(WEXITSTATUS(bracelet::unwrap(bracelet::subprocess::call(
              "false", boost::span<std::string_view>()))) == 1);
}
