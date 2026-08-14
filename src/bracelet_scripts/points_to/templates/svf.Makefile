linked.bc: $(patsubst %.cpp,%.bc,$(wildcard *.cpp)) $(patsubst %.c,%.bc,$(wildcard *.c))
	rm -f $@
	$(SVF_LLVM_LINK) -o $@ $^

%.bc: %.c
	$(SVF_CLANG) -O2 -c -emit-llvm -o $@ $<

%.bc: %.cpp
	$(SVF_CLANGXX) -O2 -c -emit-llvm -o $@ $<
