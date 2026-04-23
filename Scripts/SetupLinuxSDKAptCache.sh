#!/bin/bash
# Copyright © Unbroken AB
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# NOT part of the shipped SDK distribution. Developer convenience tool.
#
# Sets up a local TLS-MITM caching HTTP proxy (squid with ssl_bump) for the
# Linux SDK build pipeline. Terminates TLS for every apt fetch using a
# locally-generated CA, caches the decrypted response, and re-encrypts to the
# client. Build containers trust the CA so MITM is transparent and every
# archive -- HTTP or HTTPS -- caches.
#
# After running this, invoke the build driver with:
#
#   APT_PROXY=http://host.docker.internal:3128 \
#   APT_PROXY_CA=/var/lib/malterlib-apt-ca/ca.crt \
#   ./BuildLinuxSDKContainers.sh
#
# The container picks up APT_PROXY_CA, installs it into the system trust
# store, and all HTTPS apt traffic -- snapshot.ubuntu.com, apt.llvm.org,
# anything -- flows through the cache.
#
# SECURITY NOTE: the generated CA private key at /var/lib/malterlib-apt-ca/
# ca.key can impersonate any TLS host to clients that trust this CA. Keep it
# on a single dev workstation. Do NOT distribute the key, do NOT add the CA
# to the host's system trust store. The CA is meant to be trusted only inside
# short-lived build containers.
#
# Network binding: by default the proxy auto-detects the host IP that the
# build containers reach via --add-host=host.docker.internal:host-gateway,
# and listens only on that IP plus loopback. Two env overrides escape the
# auto-detection for rootless Docker, custom networks, or daemons with
# `host-gateway-ip` configured in daemon.json:
#   PROXY_LISTEN_IP=<ip>         IP the squid proxy binds to.
#   PROXY_ALLOW_SUBNET=<cidr>    Subnet permitted in the squid ACL.

set -eo pipefail

if [ "$(id -u)" -ne 0 ]; then
	exec sudo -E "$0" "$@"
fi

if ! command -v apt-get >/dev/null 2>&1; then
	echo "ERROR: This script supports Debian/Ubuntu hosts only (apt-get not found)." >&2
	exit 1
fi

echo "Installing squid-openssl + openssl..."
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y squid-openssl openssl ssl-cert

# Determine which IP the proxy should bind to so that build containers reach
# it via host.docker.internal, and which subnet to permit in the squid ACL.
# Bind narrowly so the TLS-MITM proxy (and the on-the-fly CA-signed certs it
# generates per upstream host) is not exposed on the developer's LAN.
#
# Listen IP detection:
#   * PROXY_LISTEN_IP env override wins unconditionally.
#   * Otherwise: spin up a throwaway alpine container with
#     --add-host=check:host-gateway and read what host-gateway resolved to
#     from /etc/hosts. This is exactly the address SDK build containers
#     will use, so it transparently handles a daemon.json `host-gateway-ip`
#     override, the default-bridge fallback, and custom-network setups
#     without per-mode special-casing or daemon introspection.
#
# Allow-subnet detection:
#   * PROXY_ALLOW_SUBNET env override wins unconditionally.
#   * Otherwise: the default-bridge IPv4 IPAM subnet -- that's the source
#     network of the build containers when the driver runs them on the
#     stock bridge (operators on a custom network must set the override).
ProxyListenIp="${PROXY_LISTEN_IP:-}"
ProxyAllowSubnet="${PROXY_ALLOW_SUBNET:-}"

if [ -z "$ProxyListenIp" ] || [ -z "$ProxyAllowSubnet" ]; then
	if ! command -v docker >/dev/null 2>&1; then
		echo "ERROR: docker is required to auto-detect the proxy listen IP." >&2
		echo "       Install Docker, or set PROXY_LISTEN_IP and PROXY_ALLOW_SUBNET." >&2
		exit 1
	fi
	if ! docker info >/dev/null 2>&1; then
		echo "ERROR: docker daemon is not reachable; cannot auto-detect listen IP." >&2
		echo "       Start the daemon, or set PROXY_LISTEN_IP and PROXY_ALLOW_SUBNET." >&2
		exit 1
	fi

	if [ -z "$ProxyListenIp" ]; then
		echo "Probing host-gateway via throwaway alpine container..."
		if ! ProbeOutput=$(docker run --rm --add-host=check:host-gateway alpine cat /etc/hosts 2>&1); then
			echo "ERROR: 'docker run --add-host=check:host-gateway alpine ...' failed." >&2
			echo "       Output:" >&2
			printf '         %s\n' "$ProbeOutput" >&2
			echo "       Set PROXY_LISTEN_IP=<ip> and PROXY_ALLOW_SUBNET=<cidr>" >&2
			echo "       explicitly to skip auto-detection." >&2
			exit 1
		fi
		ProxyListenIp=$(awk '$2 == "check" && $1 ~ /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ { print $1; exit }' <<< "$ProbeOutput")
		if [ -z "$ProxyListenIp" ]; then
			echo "ERROR: Could not parse an IPv4 host-gateway address from probe /etc/hosts:" >&2
			printf '         %s\n' "$ProbeOutput" >&2
			echo "       Set PROXY_LISTEN_IP=<ip> and PROXY_ALLOW_SUBNET=<cidr>" >&2
			echo "       explicitly to skip auto-detection." >&2
			exit 1
		fi
	fi

	if [ -z "$ProxyAllowSubnet" ]; then
		# Trailing `|| true` keeps `set -eo pipefail` from aborting when
		# the bridge is absent (rootless Docker, `--bridge=none`); we
		# fall through to the explicit "set PROXY_ALLOW_SUBNET" hint
		# below in that case.
		ProxyAllowSubnet=$(
			docker network inspect bridge \
				--format '{{range .IPAM.Config}}{{.Subnet}}{{"\n"}}{{end}}' 2>/dev/null \
				| awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+\// { print; exit }' \
				|| true
		)
	fi
fi

if [ -z "$ProxyListenIp" ] || [ -z "$ProxyAllowSubnet" ]; then
	echo "ERROR: Could not determine proxy listen IP / allowed subnet." >&2
	echo "       Set PROXY_LISTEN_IP=<ip> and PROXY_ALLOW_SUBNET=<cidr> explicitly." >&2
	exit 1
fi

# Squid's http_port doesn't accept unbracketed IPv6 in host:port; refuse
# anything that isn't a dotted-quad IPv4 address rather than silently
# emitting a config that fails to parse on `systemctl restart squid`.
if ! [[ "$ProxyListenIp" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
	echo "ERROR: PROXY_LISTEN_IP=$ProxyListenIp is not an IPv4 address." >&2
	echo "       Squid's http_port directive requires IPv4 here; squid would refuse to start." >&2
	exit 1
fi
if ! [[ "$ProxyAllowSubnet" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/[0-9]+$ ]]; then
	echo "ERROR: PROXY_ALLOW_SUBNET=$ProxyAllowSubnet is not an IPv4 CIDR." >&2
	exit 1
fi

echo "Proxy listen IP: $ProxyListenIp   Allowed subnet: $ProxyAllowSubnet"

CaDir=/var/lib/malterlib-apt-ca
CaCrt="$CaDir/ca.crt"
CaKey="$CaDir/ca.key"
CaPem="$CaDir/ca.pem"
CertDbDir=/var/lib/squid/ssl_db

echo "Ensuring CA under $CaDir..."
mkdir -p "$CaDir"
chmod 755 "$CaDir"
CaRegenerated=false
# Regenerate the entire CA (key, cert, combined PEM) together if any of
# the three is missing. Treating the trio as one atomic unit keeps them
# consistent: a partial cleanup that wiped just ca.pem (or just ca.crt)
# triggers a clean full regen rather than mixing old key material with
# a freshly-issued cert -- which would leave squid signing handshakes
# with one CA while containers were told to trust another.
if [ ! -f "$CaCrt" ] || [ ! -f "$CaKey" ] || [ ! -f "$CaPem" ]; then
	# ECDSA P-256: ~AES-128-equivalent security, much faster than RSA for both
	# CA signing (done once per new upstream cert) and the per-connection TLS
	# handshake the proxy performs on every bumped request.
	openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$CaKey"
	openssl req -x509 -new -nodes -key "$CaKey" -sha256 -days 3650 \
		-subj "/CN=Malterlib SDK Build Cache CA" \
		-addext "basicConstraints=critical,CA:TRUE" \
		-addext "keyUsage=critical,keyCertSign,cRLSign" \
		-out "$CaCrt"
	cat "$CaKey" "$CaCrt" > "$CaPem"
	chmod 600 "$CaKey" "$CaPem"
	chmod 644 "$CaCrt"
	CaRegenerated=true
fi
# squid runs as the proxy user and needs to read the combined PEM.
chown proxy:proxy "$CaPem" 2>/dev/null || true

if [ "$CaRegenerated" = true ] && [ -d "$CertDbDir" ]; then
	echo "Removing squid cert-gen DB because CA was regenerated..."
	rm -rf "$CertDbDir"
fi

if [ ! -d "$CertDbDir" ]; then
	echo "Initialising squid cert-gen DB at $CertDbDir..."
	mkdir -p "$(dirname "$CertDbDir")"
	/usr/lib/squid/security_file_certgen -c -s "$CertDbDir" -M 16MB
	chown -R proxy:proxy "$(dirname "$CertDbDir")"
fi

# Disable distro-provided wildcard listeners on 3128. Debian/Ubuntu package
# defaults may live in squid.conf or a conf.d file such as debian.conf; this
# script installs explicit TLS-bumped listeners below instead.
for SquidConfig in /etc/squid/squid.conf /etc/squid/conf.d/*.conf; do
	[ -f "$SquidConfig" ] || continue
	[ "$SquidConfig" = /etc/squid/conf.d/malterlib.conf ] && continue
	sed -i -E 's|^([[:space:]]*)http_port([[:space:]]+3128([[:space:]].*)?)$|\1# disabled by Malterlib SDK apt cache: http_port\2|' "$SquidConfig"
done

echo "Writing /etc/squid/conf.d/malterlib.conf..."
cat > /etc/squid/conf.d/malterlib.conf <<EOF
# Malterlib SDK builder: TLS-MITM caching proxy via squid ssl_bump.
# Loaded from /etc/squid/squid.conf via its include /etc/squid/conf.d/*.conf
# directive, which is positioned before the final 'http_access deny all'.

visible_hostname malterlib-sdk-cache

# Bind the proxy to the host IP that build containers actually reach via
# host.docker.internal (auto-detected or explicitly via PROXY_LISTEN_IP),
# plus loopback for host-side debugging. ssl-bump enables TLS interception;
# generate-host-certificates synthesises a per-hostname cert signed by our
# CA for each bumped connection.
http_port $ProxyListenIp:3128 ssl-bump \\
    cert=$CaPem \\
    generate-host-certificates=on \\
    dynamic_cert_mem_cache_size=16MB
http_port 127.0.0.1:3128 ssl-bump \\
    cert=$CaPem \\
    generate-host-certificates=on \\
    dynamic_cert_mem_cache_size=16MB

sslcrtd_program /usr/lib/squid/security_file_certgen -s $CertDbDir -M 16MB
sslcrtd_children 5

# Bump every connection; server-first so the synthesised cert mirrors the
# upstream server's CN/SAN, keeping apt's hostname verification happy.
ssl_bump server-first all

# Cache sizing tuned for repeated SDK builds. The full archive-per-arch is a
# few GB; headroom for multiple snapshot dates.
cache_dir ufs /var/spool/squid 20480 16 256
cache_mem 512 MB
maximum_object_size 2 GB
maximum_object_size_in_memory 64 MB

# Defence in depth on top of the bound interface above: only accept
# requests originating from the configured allow-subnet (where build
# containers live) or from loopback. Binding the listener already keeps
# off-host clients out; this ACL guards against a co-located container on
# an unrelated network sneaking in if the listener is ever loosened.
acl malterlib_localnet src 127.0.0.0/8
acl malterlib_localnet src $ProxyAllowSubnet
http_access allow malterlib_localnet
EOF

echo "Initialising squid cache directories..."
squid -N -z >/dev/null 2>&1 || true

echo "Restarting squid..."
systemctl restart squid
# Intentionally NOT `systemctl enable squid`: http_port binds to a Docker
# bridge IP, which only exists once dockerd has started. A boot-time
# squid would race the docker daemon and fail to bind. Operators restart
# squid manually (or rerun this script) when they want the cache up.
systemctl disable squid >/dev/null 2>&1 || true

echo
echo "Squid TLS-MITM caching proxy is running on port 3128."
echo "CA cert:   $CaCrt   (install into build containers)"
echo "CA key:    $CaKey   (keep private; DO NOT distribute)"
echo "Cache dir: /var/spool/squid"
echo
echo "NOTE: squid is NOT enabled at boot (its bind IP requires docker"
echo "      to be running first). After a reboot, run:"
echo "        sudo systemctl restart squid"
echo "      or rerun this script before kicking off an SDK build."
echo
echo "Use for SDK builds:"
echo
echo "  APT_PROXY=http://host.docker.internal:3128 APT_PROXY_CA=$CaCrt ./BuildLinuxSDKContainers.sh"
