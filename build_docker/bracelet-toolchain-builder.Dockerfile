# syntax=docker/dockerfile:1
FROM debian:trixie-slim

SHELL ["/bin/bash", "-c"]

# Install necessary system packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    curl \
    git \
    skopeo \
    xz-utils \
    && rm -rf /var/lib/apt/lists/* \
# Install Nix
    && sh <(curl --retry 5 --retry-delay 0 --proto '=https' --tlsv1.2 -L https://nixos.org/nix/install) --daemon --yes \
# Create/configure ccache
    && mkdir -m0770 -p /nix/var/cache/ccache \
    && chown --reference=/nix/store /nix/var/cache/ccache \
    && echo "experimental-features = nix-command flakes" >> /etc/nix/nix.conf \
    && echo "extra-sandbox-paths = /nix/var/cache/ccache" >> /etc/nix/nix.conf \
    && echo "sandbox = relaxed" >> /etc/nix/nix.conf \
