import { definePlan } from "@ranking-dsl/runtime";

/**
 * Test plan for verifying within-request DAG parallelism.
 *
 * Structure:
 *   viewer -> [sleep_a (20ms), sleep_b (20ms)] -> concat -> output
 *
 * With parallel execution: ~20ms (sleeps run concurrently)
 * Without parallel: ~40ms (sleeps run sequentially)
 *
 * Usage:
 *   # Sequential (default)
 *   echo '{"user_id":1}' | ./bin/rankd --plan_name parallel_sleep_plan
 *
 *   # Parallel
 *   echo '{"user_id":1}' | ./bin/rankd --plan_name parallel_sleep_plan --within_request_parallelism
 *
 *   # Benchmark comparison
 *   ./bin/rankd --plan_name parallel_sleep_plan --bench 10
 */
export default definePlan({
  name: "parallel_sleep_plan",
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.redis_default });

    // Two parallel branches with sleep
    const branch_a = source.sleep({ durationMs: 20 });
    const branch_b = source.sleep({ durationMs: 20 });

    // Merge branches
    const merged = branch_a.concat({ rhs: branch_b });

    return merged;
  },
});
