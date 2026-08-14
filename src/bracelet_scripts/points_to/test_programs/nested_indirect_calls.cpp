#include "common.h"
#include <iostream>

bool which[] = {true, false, true, false};
bool which2[] = {true, true, false, false};

namespace {

struct Thing {
  virtual ~Thing() {}
  virtual void do_it(int y) = 0;
};

struct Thing1 : public Thing {
  Thing1(int x) : x(x) {}
  virtual void do_it(int y) override {
    std::cout << "Thing1 says " << x << " " << y << "\n";
  }

private:
  int x;
};

struct Thing2 : public Thing {
  Thing2(int x) : x(x) {}
  virtual void do_it(int y) override {
    std::cout << "Thing2 says " << x << " " << y << "\n";
  }

private:
  int x;
};

struct Wrapper {
  virtual ~Wrapper() {}
  virtual void do_it() = 0;
};
struct Wrapper1 : public Wrapper {
  Wrapper1(Thing *thing) : thing(thing) {}
  virtual void do_it() override { thing->do_it(42); }

private:
  Thing *thing;
};
struct Wrapper2 : public Wrapper {
  Wrapper2(Thing *thing) : thing(thing) {}
  virtual void do_it() override { thing->do_it(43); }

private:
  Thing *thing;
};

void __attribute__((noinline)) invoke_do_it(Wrapper *w) { w->do_it(); }

} // namespace

static void __attribute__((noinline)) body() {
  Thing1 t1(12);
  Thing2 t2(13);
  for (size_t i = 0; i < sizeof(which); i++) {
    Thing *t = &t1;
    if (which[i])
      t = &t2;
    Wrapper *w = new Wrapper1(t);
    if (which2[i]) {
      delete w;
      w = new Wrapper2(t);
    }
    invoke_do_it(w);
    delete w;
  }
}
MAIN(body)
