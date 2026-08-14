ARG LLVM_IMAGE
ARG SVF_IMAGE

FROM ${SVF_IMAGE} as svf
FROM ${LLVM_IMAGE} as build

COPY --from=svf /opt/svf /opt/svf
COPY --from=ghcr.io/astral-sh/uv:0.8.13 /uv /uvx /usr/local/bin/
COPY . /bracelet-scripts
RUN cd /bracelet-scripts && \
  mkdir -p /opt/bracelet-llvm && \
  echo ${LLVM_IMAGE} > /opt/bracelet-llvm/llvm_image.txt && \
  build_base/build_ci.sh

ARG LIBALLOCS_IMAGE
FROM ${LIBALLOCS_IMAGE} as liballocs

# if ENABLE_LIBALLOCS is set to 1, then
# we will run /opt/liballocs/liballocs-setup-runtime-image.sh,
# which creates /usr/lib/meta, installs
# the same set of packages as used for building liballocs,
# moves the bracelet-cc.sh on top of bracelet-clang.sh (WHY?).
# and then copies the liballocs/bracelet-cc.sh to /opt/bracelet-llvm/bin.
# XXX: we never set LD_PRELOAD?
#
# 0. no liballocs
# 1. build fully liballocswise, but don't run with liballocs (the current?)
# 2. build only metadata,       and do    run with liballocs  <-- how do we do this? by adding just the linker plugin to flags
# 3. build fully liballocswise  and do    run with liballocs
ARG ENABLE_LIBALLOCS=0
ARG LLVM_IMAGE
FROM ${LLVM_IMAGE}
COPY --from=build /opt/bracelet-llvm /opt/bracelet-llvm
ENV CC=/opt/bracelet-llvm/bin/bracelet-cc.sh
ENV CXX=/opt/bracelet-llvm/bin/bracelet-c++.sh
ENV TRIPLET=x64-linux-bracelet
ENV ORIG_TOOLCHAIN_DIR=/opt/bracelet-llvm/toolchain
ENV TOOLCHAIN_FILE=/opt/bracelet-llvm/toolchain/bracelet-toolchain.cmake

# Hack to copy in triplets to where the downstream CI expects them
RUN cp /opt/bracelet-llvm/toolchain/* /opt/vcpkg-overlays/triplets/
RUN echo /opt/bracelet-llvm/bin > /etc/ld.so.conf.d/bracelet.conf
RUN echo /opt/bracelet-llvm/lib >> /etc/ld.so.conf.d/bracelet.conf
RUN echo /opt/galois-llvm-build/lib >> /etc/ld.so.conf.d/bracelet.conf
RUN ldconfig

# Set BRACELET_TRACE_DIR in case we try to run anything in a build environment
RUN mkdir -p /bracelet-trace && chmod 777 /bracelet-trace
ENV BRACELET_TRACE_DIR=/bracelet-trace

COPY --from=liballocs /opt/liballocs /opt/liballocs
ENV ENABLE_LIBALLOCS=${ENABLE_LIBALLOCS}
ENV LIBALLOCS=/opt/liballocs
RUN bash -c 'set -euxo pipefail; if [[ ${ENABLE_LIBALLOCS} -ge 1 ]]; then /opt/liballocs/liballocs-setup-runtime-image.sh; fi'
