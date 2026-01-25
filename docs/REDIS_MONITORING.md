# Redis QPS Monitoring

This guide covers monitoring Redis QPS and other metrics for the dag-executor project.

## Quick Stats (No Setup Required)

```bash
# Current instantaneous QPS
redis-cli INFO stats | grep instantaneous_ops_per_sec

# Watch stats live (every second)
watch -n 1 'redis-cli INFO stats | grep ops_per_sec'

# Built-in stat mode
redis-cli --stat -i 1

# Slow queries
redis-cli SLOWLOG GET 10
```

## RedisTimeSeries (Self-Contained Monitoring)

If your Redis has the TimeSeries module (check with `redis-cli MODULE LIST`), you can store metrics inside Redis itself.

### Setup

```bash
# Create time series with 2-hour retention
redis-cli TS.CREATE stats:ops_per_sec RETENTION 7200000 DUPLICATE_POLICY LAST
redis-cli TS.CREATE stats:connections RETENTION 7200000 DUPLICATE_POLICY LAST
redis-cli TS.CREATE stats:memory RETENTION 7200000 DUPLICATE_POLICY LAST
```

### Collector (Run Inside Container)

For Redis running in Docker/Orbstack:

```bash
# Start collector inside container (runs in background)
docker exec -d dag-redis sh -c '
while true; do
  OPS=$(redis-cli INFO stats | grep instantaneous_ops_per_sec | cut -d: -f2 | tr -d "\r")
  redis-cli TS.ADD stats:ops_per_sec "*" $OPS > /dev/null 2>&1
  sleep 5
done
'
```

For local Redis:

```bash
# Collector script
while true; do
  OPS=$(redis-cli INFO stats | grep instantaneous_ops_per_sec | cut -d: -f2 | tr -d '\r')
  redis-cli TS.ADD stats:ops_per_sec '*' $OPS > /dev/null
  sleep 5
done
```

### Querying

```bash
# Latest value
redis-cli TS.GET stats:ops_per_sec

# Last N samples (most recent first)
redis-cli TS.REVRANGE stats:ops_per_sec - + COUNT 10

# All samples from last 5 minutes
redis-cli TS.RANGE stats:ops_per_sec $(( $(date +%s)*1000 - 300000 )) +

# Aggregated: average per minute
redis-cli TS.RANGE stats:ops_per_sec - + AGGREGATION avg 60000

# Aggregated: max per 10-second buckets
redis-cli TS.RANGE stats:ops_per_sec - + AGGREGATION max 10000
```

### How It Works

```
┌─────────────────────────────────────────────────────────────┐
│  Collector (every 5s)                                       │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ redis-cli TS.ADD stats:ops_per_sec '*' <value>      │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                  │
│  Redis TimeSeries (in-memory, compressed)                   │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ stats:ops_per_sec: [(ts1,v1), (ts2,v2), ...]        │   │
│  │ RETENTION 7200000ms (2 hours)                        │   │
│  │ Auto-expires old data                                │   │
│  └─────────────────────────────────────────────────────┘   │
│                          ↓                                  │
│  Query anytime                                              │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ TS.RANGE stats:ops_per_sec - + AGGREGATION avg 60000│   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Production Monitoring Options

| Approach | Pros | Cons |
|----------|------|------|
| **RedisTimeSeries** | Self-contained, no external deps | Limited visualization |
| **Prometheus + redis_exporter** | Industry standard, Grafana dashboards | Extra infrastructure |
| **Datadog/New Relic** | Full-featured, managed | Cost, vendor lock-in |
| **AWS CloudWatch** (ElastiCache) | Native integration | AWS-only |

### Prometheus + redis_exporter

```bash
# Install
brew install prometheus redis_exporter

# Run exporter (exposes metrics at :9121)
redis_exporter --redis.addr=localhost:6379 &

# Key metrics exposed:
# - redis_commands_processed_total (rate = QPS)
# - redis_connected_clients
# - redis_memory_used_bytes
# - redis_keyspace_hits / redis_keyspace_misses
```

## Why Redis Doesn't Log QPS

Redis logs (`docker logs dag-redis`) only contain:
- Server startup/shutdown
- Client connections (if configured)
- Warnings and errors
- BGSAVE events

Redis doesn't log QPS because:
1. Single-threaded, optimized for speed
2. Logging every command would add overhead
3. Philosophy: metrics delegated to external systems

Use `INFO stats` or TimeSeries for metrics instead.
