ARG BUILD_BASE
ARG RUN_BASE_PARENT
ARG ECFS_IMAGE

FROM ${ECFS_IMAGE} as ecfs

FROM ${BUILD_BASE} as build_base
COPY ./root_snapshot.c /tmp/root_snapshot.c
RUN mkdir -p /opt/bracelet-llvm/bin/ && \
  cc -o /opt/bracelet-llvm/bin/root_snapshot /tmp/root_snapshot.c && \
  chmod 4755 /opt/bracelet-llvm/bin/root_snapshot

ARG RUN_BASE_PARENT
FROM ${RUN_BASE_PARENT}
COPY --from=build_base /opt/bracelet-llvm/bin/libbracelet_pointsto_trace_runtime.so /opt/bracelet-llvm/bin/libbracelet_pointsto_trace_runtime.so
COPY --from=ecfs /opt/ecfs/bin /opt/ecfs/bin
RUN /opt/ecfs/bin/setup-ecfs-runtime.sh
RUN apt-get update && apt-get install -y python3 gdb zstd
ARG COREDUMP_DST
RUN mkdir -p /opt/bracelet-llvm && \
  echo "|/opt/bracelet-llvm/coredump-wrapper.sh %p" > /etc/core_pattern
COPY ./coredump.sh /opt/bracelet-llvm/
COPY ./coredump-wrapper.sh /opt/bracelet-llvm/
RUN echo ${COREDUMP_DST} > /opt/bracelet-llvm/coredump-dst.txt && \
  bash -c '[[ $(cat /opt/bracelet-llvm/coredump-dst.txt | wc -c) > 1 ]]'
COPY ./coredump.sh /opt/bracelet-llvm/coredump.sh
COPY --from=build_base /opt/bracelet-llvm/bin/root_snapshot /opt/bracelet-llvm/bin/root_snapshot
COPY ./snapshot.py /opt/bracelet-llvm/bin/snapshot.py
COPY ./snapshot-launcher.sh /opt/bracelet-llvm/bin/snapshot-launcher.sh
COPY ./entrypoint.py /opt/bracelet-llvm/bin/entrypoint.py
RUN mkdir -p /bracelet-trace && chmod 777 /bracelet-trace
RUN echo /opt/bracelet-llvm/bin > /etc/ld.so.conf.d/bracelet.conf
RUN echo /opt/bracelet-llvm/lib >> /etc/ld.so.conf.d/bracelet.conf
RUN echo /opt/galois-llvm-build/lib >> /etc/ld.so.conf.d/bracelet.conf
RUN ldconfig
ENV BRACELET_TRACE_DIR=/bracelet-trace
ENTRYPOINT ["/opt/bracelet-llvm/bin/entrypoint.py"]
