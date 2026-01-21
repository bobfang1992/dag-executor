#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

COMPOSE_PROJECT="rankingdsl-redis"

echo "Stopping Redis container..."
docker compose -p "$COMPOSE_PROJECT" -f "$REPO_ROOT/docker/redis.compose.yml" down -v || true
echo "Redis stopped."
