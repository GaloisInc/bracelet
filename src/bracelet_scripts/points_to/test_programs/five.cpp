// Test argument-less function (pointers)
#include "common.h"
#include <iostream>

bool which[] = {true, false, true, false};

namespace {

struct Thing {
  virtual ~Thing() {}
  virtual void do_it() = 0;
};

struct Thing1 : public Thing {
  Thing1(int x) : x(x) {}
  virtual void do_it() override { std::cout << "Thing1 says " << x << "\n"; }

private:
  int x;
};

struct Thing2 : public Thing {
  Thing2(int x) : x(x) {}
  virtual void do_it() override { std::cout << "Thing2 says " << x << "\n"; }

private:
  int x;
};

} // namespace

static void __attribute__((noinline)) body() {
  Thing1 t1(12);
  Thing2 t2(13);
  for (size_t i = 0; i < sizeof(which); i++) {
    Thing *t = &t1;
    if (which[i])
      t = &t2;
    t->do_it();
  }
}
MAIN(body)
