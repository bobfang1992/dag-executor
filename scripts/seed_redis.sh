#!/usr/bin/env bash
# Seed Redis with test data for CI
# Used by: engine tests, plan execution tests
#
# Data required:
#   user:{uid} HASH - country field (viewer task)
#   follow:{uid} LIST - user IDs (follow task)
#   recommendation:{uid} LIST - user IDs (recommendation task)
#   media:{id} LIST - media IDs (media task)

set -euo pipefail

echo "Seeding Redis test data..."

# Wait for Redis to be ready
for i in {1..30}; do
  if redis-cli ping 2>/dev/null | grep -q PONG; then
    echo "Redis is ready"
    break
  fi
  if [ "$i" -eq 30 ]; then
    echo "ERROR: Redis failed to start"
    exit 1
  fi
  echo "Waiting for Redis... ($i/30)"
  sleep 1
done

# Clear existing test data
redis-cli FLUSHDB >/dev/null

# User data (viewer task)
# user:1 - primary test user
redis-cli HSET user:1 country US >/dev/null
# user:123 - for request parsing tests
redis-cli HSET user:123 country CA >/dev/null
# user:456 - for request parsing tests
redis-cli HSET user:456 country UK >/dev/null
# user:789 - for request parsing tests
redis-cli HSET user:789 country MX >/dev/null

# Follow lists (follow task)
# user 1 follows users 100-109
redis-cli RPUSH follow:1 100 101 102 103 104 105 106 107 108 109 >/dev/null
# user 123 follows users 200-204
redis-cli RPUSH follow:123 200 201 202 203 204 >/dev/null

# Recommendation lists (recommendation task)
# recommendations for user 1
redis-cli RPUSH recommendation:1 500 501 502 503 504 505 506 507 508 509 >/dev/null
# recommendations for user 123
redis-cli RPUSH recommendation:123 600 601 602 603 604 >/dev/null

# Media lists (media task)
# media for various row IDs that might come from follow/recommendation
redis-cli RPUSH media:100 1000 1001 1002 >/dev/null
redis-cli RPUSH media:101 1010 1011 1012 >/dev/null
redis-cli RPUSH media:500 5000 5001 5002 >/dev/null
redis-cli RPUSH media:501 5010 5011 5012 >/dev/null

echo "Redis seeded successfully"
echo "  - $(redis-cli DBSIZE | cut -d' ' -f2) keys"
