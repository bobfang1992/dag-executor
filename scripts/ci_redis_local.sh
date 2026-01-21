#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Always tear down Redis on exit (success or failure)
trap "$SCRIPT_DIR/redis_down.sh" EXIT

echo "=== Step 14.3: Local Redis Harness ==="
echo ""

# Start Redis
"$SCRIPT_DIR/redis_up.sh"

echo ""

# Run seed script
echo "Running seed script..."
"$REPO_ROOT/node_modules/.bin/tsx" "$REPO_ROOT/dsl/tools/redis_seed.ts"

echo ""
echo "=== All done! ==="
