#!/usr/bin/env bash
# Records this machine's jitter floor. Run it before optimising anything, and
# again after tuning, so improvements can be attributed to the tuning rather
# than to luck.
#
# Uses cyclictest when it is available (it measures true wakeup latency from
# the kernel's timer, which needs root), and always runs the in-tree
# jitter_probe, which needs nothing and works everywhere including WSL.
set -euo pipefail

SECONDS_TO_RUN="${1:-10}"
BUILD="${BUILD:-build/release}"

echo "=== machine ==="
uname -srm
grep -m1 "model name" /proc/cpuinfo 2>/dev/null || true
echo "cores: $(nproc 2>/dev/null || echo '?')"
echo "governor: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo 'n/a')"
echo "tsc flags: $(grep -m1 -o 'constant_tsc\|nonstop_tsc' /proc/cpuinfo 2>/dev/null | sort -u | tr '\n' ' ')"
echo "isolcpus: $(cat /sys/devices/system/cpu/isolated 2>/dev/null || echo 'none')"
echo "THP: $(cat /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo 'n/a')"
echo

if [[ ! -x "${BUILD}/jitter_probe" ]]; then
  echo "error: ${BUILD}/jitter_probe not found. Build first:" >&2
  echo "  cmake --preset release && cmake --build build/release" >&2
  exit 1
fi

echo "=== jitter_probe (${SECONDS_TO_RUN}s) ==="
"${BUILD}/jitter_probe" "${SECONDS_TO_RUN}"
echo

if command -v cyclictest >/dev/null 2>&1; then
  echo "=== cyclictest (${SECONDS_TO_RUN}s) ==="
  cyclictest --mlockall --priority=80 --interval=200 \
             --distance=0 --duration="${SECONDS_TO_RUN}" --quiet --histogram=1000 \
    || echo "cyclictest failed (needs root for --priority)"
else
  echo "=== cyclictest: not installed ==="
  echo "optional: apt-get install rt-tests, then re-run as root for true wakeup latency."
  echo "jitter_probe above already gives the userspace floor, which is what bounds this project."
fi
