#include "../mul/mul.h"

#define KIT_TEST_FILE mul
#include <kit_test/test.h>

TEST("mul 2, 3") {
  REQUIRE(mul(2, 3) == 6);
}

TEST("mul 42, -1") {
  REQUIRE(mul(42, -1) == -42);
}
