# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Malterlib LLVM distribution builder image - Ubuntu 22.04.
#
# The base image is pinned by multi-arch index digest so local Docker builds
# remain tied to the same Ubuntu release image even if docker.io retags
# ubuntu:22.04. The wrapper script bind-mounts the source checkout and keeps
# build output on the host, so interrupted builds resume from the same CMake /
# Ninja state on the next run.

FROM ubuntu:22.04@sha256:962f6cadeae0ea6284001009daa4cc9a8c37e75d1f5191cf0eb83fe565b63dd7

ARG APT_PROXY=

ENV DEBIAN_FRONTEND=noninteractive

RUN if [ -n "${APT_PROXY}" ]; then \
		echo "Acquire::http::Proxy \"${APT_PROXY}\";" > /etc/apt/apt.conf.d/01proxy; \
		echo "Acquire::https::Proxy \"${APT_PROXY}\";" >> /etc/apt/apt.conf.d/01proxy; \
	fi \
	&& apt-get update \
	&& apt-get install -y --no-install-recommends \
		binutils-dev \
		build-essential \
		ca-certificates \
		cmake \
		git \
		liblzma-dev \
		libpfm4-dev \
		libssl-dev \
		libzstd-dev \
		ncurses-bin \
		ninja-build \
		patchelf \
		pkg-config \
		python3 \
		python3-dev \
		python3-pip \
		python3.10-dev \
		python3-venv \
		xz-utils \
		zlib1g-dev \
		zstd \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /Source/Malterlib/Malterlib/Tool/Scripts
