#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "Starting Redis container..."
docker compose -f "$REPO_ROOT/docker/redis.compose.yml" up -d

echo "Waiting for Redis to be healthy..."
timeout=30
while [ $timeout -gt 0 ]; do
  if docker compose -f "$REPO_ROOT/docker/redis.compose.yml" ps --format json | grep -q '"Health":"healthy"'; then
    echo "Redis is healthy!"
    exit 0
  fi
  sleep 1
  timeout=$((timeout - 1))
done

echo "ERROR: Redis failed to become healthy within 30 seconds"
exit 1
