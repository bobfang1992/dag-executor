#!/usr/bin/env bash
# Monitor CPU and memory on a remote EC2 instance with rolling output.
# Usage: ./scripts/monitor_ec2.sh [host] [interval_seconds]
#
# Examples:
#   ./scripts/monitor_ec2.sh 52.23.153.113
#   ./scripts/monitor_ec2.sh 52.23.153.113 5
#   ./scripts/monitor_ec2.sh bench-64 2

HOST="${1:-52.23.153.113}"
INTERVAL="${2:-3}"

echo "Monitoring $HOST every ${INTERVAL}s  (Ctrl-C to stop)"
echo "------------------------------------------------------------------------"
printf "%-19s  %6s  %8s  %8s  %8s  %8s\n" \
       "TIMESTAMP" "CPU%" "MEM_USED" "MEM_FREE" "MEM_TOT" "MEM%"
echo "------------------------------------------------------------------------"

while true; do
  ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no "$HOST" '
    # CPU: average idle over 1 second, compute used%
    cpu_idle=$(top -bn2 -d0.5 | grep "Cpu(s)" | tail -1 | awk "{print \$8}" | tr -d "%id,")
    cpu_used=$(awk "BEGIN{printf \"%.1f\", 100 - $cpu_idle}")

    # Memory from /proc/meminfo (works on any Linux)
    eval $(awk "/MemTotal/{t=\$2} /MemAvailable/{a=\$2} END{printf \"mt=%d ma=%d\",t,a}" /proc/meminfo)
    mem_used_mb=$(( (mt - ma) / 1024 ))
    mem_free_mb=$(( ma / 1024 ))
    mem_total_mb=$(( mt / 1024 ))
    mem_pct=$(awk "BEGIN{printf \"%.1f\", 100 * (1 - $ma/$mt)}")

    printf "%s  %5s%%  %6sMB  %6sMB  %6sMB  %5s%%\n" \
      "$(date +%Y-%m-%dT%H:%M:%S)" \
      "$cpu_used" "$mem_used_mb" "$mem_free_mb" "$mem_total_mb" "$mem_pct"
  ' 2>/dev/null || printf "%-19s  %s\n" "$(date +%Y-%m-%dT%H:%M:%S)" "CONNECTION FAILED"

  sleep "$INTERVAL"
done
