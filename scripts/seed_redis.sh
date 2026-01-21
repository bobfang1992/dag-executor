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

# User data (viewer task and followee hydration)
# user:1 - primary test user (viewer)
redis-cli HSET user:1 country US >/dev/null
# user:123 - for request parsing tests
redis-cli HSET user:123 country CA >/dev/null
# user:456 - for request parsing tests
redis-cli HSET user:456 country UK >/dev/null
# user:789 - for request parsing tests
redis-cli HSET user:789 country MX >/dev/null

# Followee user data (IDs 1-10 with alternating countries)
# Odd IDs = US, Even IDs = CA
# This supports regex tests that filter by country
redis-cli HSET user:2 country CA >/dev/null
redis-cli HSET user:3 country US >/dev/null
redis-cli HSET user:4 country CA >/dev/null
redis-cli HSET user:5 country US >/dev/null
redis-cli HSET user:6 country CA >/dev/null
redis-cli HSET user:7 country US >/dev/null
redis-cli HSET user:8 country CA >/dev/null
redis-cli HSET user:9 country US >/dev/null
redis-cli HSET user:10 country CA >/dev/null

# Recommendation user data (IDs 1001-1010)
# All have country = US
redis-cli HSET user:1001 country US >/dev/null
redis-cli HSET user:1002 country US >/dev/null
redis-cli HSET user:1003 country US >/dev/null
redis-cli HSET user:1004 country US >/dev/null
redis-cli HSET user:1005 country US >/dev/null
redis-cli HSET user:1006 country US >/dev/null
redis-cli HSET user:1007 country US >/dev/null
redis-cli HSET user:1008 country US >/dev/null
redis-cli HSET user:1009 country US >/dev/null
redis-cli HSET user:1010 country US >/dev/null

# Follow lists (follow task)
# user 1 follows users 1-10 (tests expect IDs [3,4,5,6,7] after vm + filter + take)
# The demo plan computes final_score = id * 0.2, filters >= 0.6, takes 5
redis-cli RPUSH follow:1 1 2 3 4 5 6 7 8 9 10 >/dev/null
# user 123 follows users 1-5
redis-cli RPUSH follow:123 1 2 3 4 5 >/dev/null

# Recommendation lists (recommendation task)
# recommendations for user 1 - IDs starting from 1001 (for concat_plan test)
redis-cli RPUSH recommendation:1 1001 1002 1003 1004 1005 1006 1007 1008 1009 1010 >/dev/null
# recommendations for user 123
redis-cli RPUSH recommendation:123 1021 1022 1023 1024 1025 >/dev/null

# Media lists (media task)
# media for various row IDs that might come from follow/recommendation
redis-cli RPUSH media:1 1001 1002 1003 >/dev/null
redis-cli RPUSH media:2 1004 1005 1006 >/dev/null
redis-cli RPUSH media:11 1101 1102 1103 >/dev/null
redis-cli RPUSH media:12 1104 1105 1106 >/dev/null

echo "Redis seeded successfully"
echo "  - $(redis-cli DBSIZE | cut -d' ' -f2) keys"
