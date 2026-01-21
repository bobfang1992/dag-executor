#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

COMPOSE_PROJECT="rankingdsl-redis"

echo "Starting Redis container..."
docker compose -p "$COMPOSE_PROJECT" -f "$REPO_ROOT/docker/redis.compose.yml" up -d

echo "Waiting for Redis to be healthy..."
CONTAINER_NAME="${COMPOSE_PROJECT}-redis-1"
timeout=30
while [ $timeout -gt 0 ]; do
  if docker exec "$CONTAINER_NAME" redis-cli ping 2>/dev/null | grep -q "PONG"; then
    echo "Redis is healthy!"
    exit 0
  fi
  sleep 1
  timeout=$((timeout - 1))
done

echo "ERROR: Redis failed to respond to ping within 30 seconds"
exit 1
