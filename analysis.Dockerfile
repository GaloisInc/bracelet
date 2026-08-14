ARG SVF_IMAGE
ARG BUILD_BASE
ARG SCREACH_IMAGE

FROM ${SVF_IMAGE} as svf

FROM ${SCREACH_IMAGE} as screach

FROM ${BUILD_BASE}

RUN apt-get update
RUN apt-get install -y zstd jq

COPY --from=ghcr.io/astral-sh/uv:0.8.13 /uv /uvx /usr/local/bin/
COPY --from=svf /opt/svf /opt/svf
COPY --from=screach /usr/local/bin/screach \  
    /usr/local/bin/cvc4 \
    /usr/local/bin/cvc5 \
    /usr/local/bin/yices \
    /usr/local/bin/z3 \
    /usr/local/bin/
    
RUN /opt/svf/galois-setup-svf.sh

WORKDIR /bracelet-scripts

COPY ./ /bracelet-scripts

RUN uv sync --locked --no-dev

RUN apt-get install -y libffi-dev libncurses5-dev libsqlite3-dev libncurses6 mcpp

RUN curl -L --output souffle.deb  https://github.com/souffle-lang/souffle/releases/download/2.5/x86_64-ubuntu-2404-souffle-2.5-Linux.deb 
RUN dpkg -i souffle.deb

ENV BRACELET_BIN_DIR=/opt/bracelet-llvm/bin
ENV PATH="/bracelet-scripts/.venv/bin:$PATH"

RUN mkdir -p /opt/bracelet-llvm-runtime/bin; mv /opt/bracelet-llvm/bin/libbracelet_pointsto_trace_runtime.so /opt/bracelet-llvm-runtime/bin/

CMD [ "python", "/bracelet-scripts/src/bracelet_scripts/entrypoint.py", "--bracelet-edges=/opt/bracelet-llvm/bin/bracelet-edges", "--run-cg-filter"]
