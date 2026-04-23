# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Malterlib Linux SDK builder image - Debian.
#
# This image is deliberately minimal: BuildLinuxSDK.sh installs all SDK
# dependencies at run time so the same script works in the container and on
# a plain developer machine. The image only provides a non-root build user
# with passwordless sudo so the script's sudo calls succeed.
#
# Used for linux/386 via docker buildx --platform. Debian is used for the
# i386 SDK because Ubuntu no longer publishes an i386 port.
#
# The base image is pinned by multi-arch index digest so that rebuilds remain
# reproducible even when docker.io retags debian:12. Update by running
# Scripts/UpdateLinuxSDKBaseImages.sh, which refreshes every pin in this tree.

FROM debian:12@sha256:8a8cd02c5912770b4980228a54d4aff9e4f986f1eb2525d2d371dec5232cefcc

ARG BUILD_UID=1000
ARG BUILD_GID=1000
ARG BUILD_USER=builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
	&& apt-get install -y --no-install-recommends sudo ca-certificates \
	&& rm -rf /var/lib/apt/lists/* \
	&& (getent group ${BUILD_USER} >/dev/null || groupadd -o -g ${BUILD_GID} ${BUILD_USER}) \
	&& (getent passwd ${BUILD_USER} >/dev/null || useradd -o -m -u ${BUILD_UID} -g ${BUILD_GID} -s /bin/bash ${BUILD_USER}) \
	&& echo "${BUILD_USER} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${BUILD_USER} \
	&& chmod 0440 /etc/sudoers.d/${BUILD_USER}

USER ${BUILD_USER}
WORKDIR /home/${BUILD_USER}
