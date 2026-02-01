// libuv micro-benchmarks v3
// Methodology: 2 warm-up runs + 3 measured runs, report median
// Tests: post throughput, timer throughput (1ms), NO coroutine test (libuv has no coro)
//
// Build: gcc -O3 -o bench_libuv bench_libuv_v3.c -luv -lpthread -lm
// Run:   taskset -c 0-1 ./bench_libuv

#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sys/resource.h>
#include <time.h>

// ============================================================
// Helpers
// ============================================================
static int64_t get_rss_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

typedef struct {
    double min_us, max_us, mean_us, p50_us, p90_us, p99_us;
    int count;
} LatStats;

static LatStats compute_stats(double* v, int n) {
    qsort(v, n, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    LatStats s;
    s.min_us = v[0]; s.max_us = v[n-1]; s.mean_us = sum / n;
    s.p50_us = v[n*50/100]; s.p90_us = v[n*90/100]; s.p99_us = v[n*99/100];
    s.count = n;
    return s;
}

static void print_lat(const char* prefix, LatStats* s) {
    printf("    \"%slatency\": {\n", prefix);
    printf("      \"min_us\": %.1f,\n", s->min_us);
    printf("      \"max_us\": %.1f,\n", s->max_us);
    printf("      \"mean_us\": %.1f,\n", s->mean_us);
    printf("      \"p50_us\": %.1f,\n", s->p50_us);
    printf("      \"p90_us\": %.1f,\n", s->p90_us);
    printf("      \"p99_us\": %.1f,\n", s->p99_us);
    printf("      \"count\": %d\n", s->count);
    printf("    }");
}

// ============================================================
// 1. Post throughput: cross-thread uv_async_send
// ============================================================
// uv_async_send coalesces signals, so we use a queue + uv_check_t
// to drain all pending callbacks per loop iteration.

typedef struct {
    uv_loop_t* loop;
    uv_async_t async;
    uv_check_t check;
    pthread_mutex_t mu;
    int* queue;
    int queue_len;
    int queue_cap;
    int total;
    int completed;
} PostCtx;

static void post_check_cb(uv_check_t* handle) {
    PostCtx* ctx = (PostCtx*)handle->data;
    int batch;
    pthread_mutex_lock(&ctx->mu);
    batch = ctx->queue_len;
    ctx->queue_len = 0;
    pthread_mutex_unlock(&ctx->mu);

    ctx->completed += batch;
    if (ctx->completed >= ctx->total) {
        uv_check_stop(&ctx->check);
        uv_close((uv_handle_t*)&ctx->check, NULL);
        uv_close((uv_handle_t*)&ctx->async, NULL);
    }
}

static void post_async_cb(uv_async_t* handle) {
    // Just a wakeup — actual draining happens in check_cb
}

typedef struct {
    double wall_ms;
    double ops_per_sec;
} PostResult;

static void* post_loop_thread(void* arg) {
    PostCtx* ctx = (PostCtx*)arg;
    uv_run(ctx->loop, UV_RUN_DEFAULT);
    return NULL;
}

static PostResult run_post(int total) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    PostCtx ctx;
    ctx.loop = &loop;
    ctx.total = total;
    ctx.completed = 0;
    ctx.queue_cap = total;
    ctx.queue = (int*)malloc(total * sizeof(int));
    ctx.queue_len = 0;
    pthread_mutex_init(&ctx.mu, NULL);

    uv_async_init(&loop, &ctx.async, post_async_cb);
    ctx.async.data = &ctx;

    uv_check_init(&loop, &ctx.check);
    ctx.check.data = &ctx;
    uv_check_start(&ctx.check, post_check_cb);

    pthread_t tid;
    pthread_create(&tid, NULL, post_loop_thread, &ctx);

    double start = now_us();
    for (int i = 0; i < total; i++) {
        pthread_mutex_lock(&ctx.mu);
        ctx.queue[ctx.queue_len++] = 1;
        pthread_mutex_unlock(&ctx.mu);
        uv_async_send(&ctx.async);
    }

    pthread_join(tid, NULL);
    double elapsed_ms = (now_us() - start) / 1e3;

    free(ctx.queue);
    pthread_mutex_destroy(&ctx.mu);
    uv_loop_close(&loop);

    PostResult r;
    r.wall_ms = elapsed_ms;
    r.ops_per_sec = total / (elapsed_ms / 1e3);
    return r;
}

static void bench_post(int total, int warmup, int runs) {
    for (int i = 0; i < warmup; i++) run_post(total);
    PostResult results[10];
    for (int i = 0; i < runs; i++) results[i] = run_post(total);
    // Sort by wall_ms, take median
    for (int i = 0; i < runs - 1; i++)
        for (int j = i + 1; j < runs; j++)
            if (results[j].wall_ms < results[i].wall_ms) {
                PostResult tmp = results[i]; results[i] = results[j]; results[j] = tmp;
            }
    PostResult* med = &results[runs/2];
    printf("  \"posts\": {\n");
    printf("    \"total_posts\": %d,\n", total);
    printf("    \"warmup_runs\": %d,\n", warmup);
    printf("    \"measured_runs\": %d,\n", runs);
    printf("    \"wall_ms\": %.1f,\n", med->wall_ms);
    printf("    \"posts_per_sec\": %.0f,\n", med->ops_per_sec);
    printf("    \"rss_kb\": %ld\n", (long)get_rss_kb());
    printf("  }");
}

// ============================================================
// 2. Timer throughput: 10K × 1ms timers
// ============================================================
typedef struct {
    double* latencies;
    int* fired;
    int total;
    uv_loop_t* loop;
    double created;
} TimerCtx;

static void timer_cb(uv_timer_t* handle) {
    TimerCtx* ctx = (TimerCtx*)handle->data;
    double lat = now_us() - ctx->created;
    ctx->latencies[*ctx->fired] = lat;
    (*ctx->fired)++;
    uv_timer_stop(handle);
    uv_close((uv_handle_t*)handle, (uv_close_cb)free);
    free(ctx);
}

typedef struct {
    double wall_ms; double timers_per_sec; LatStats lat;
} TimerResult;

static TimerResult run_timers(int total) {
    uv_loop_t loop;
    uv_loop_init(&loop);

    double* latencies = (double*)malloc(total * sizeof(double));
    int fired = 0;

    double start = now_us();
    for (int i = 0; i < total; i++) {
        uv_timer_t* t = (uv_timer_t*)malloc(sizeof(uv_timer_t));
        uv_timer_init(&loop, t);
        TimerCtx* ctx = (TimerCtx*)malloc(sizeof(TimerCtx));
        ctx->latencies = latencies;
        ctx->fired = &fired;
        ctx->total = total;
        ctx->loop = &loop;
        ctx->created = now_us();
        t->data = ctx;
        uv_timer_start(t, timer_cb, 1, 0);
    }

    uv_run(&loop, UV_RUN_DEFAULT);
    double elapsed_ms = (now_us() - start) / 1e3;
    uv_loop_close(&loop);

    LatStats stats = compute_stats(latencies, total);
    free(latencies);

    TimerResult r;
    r.wall_ms = elapsed_ms;
    r.timers_per_sec = total / (elapsed_ms / 1e3);
    r.lat = stats;
    return r;
}

static void bench_timers(int total, int warmup, int runs) {
    for (int i = 0; i < warmup; i++) run_timers(total);
    TimerResult results[10];
    for (int i = 0; i < runs; i++) results[i] = run_timers(total);
    for (int i = 0; i < runs - 1; i++)
        for (int j = i + 1; j < runs; j++)
            if (results[j].wall_ms < results[i].wall_ms) {
                TimerResult tmp = results[i]; results[i] = results[j]; results[j] = tmp;
            }
    TimerResult* med = &results[runs/2];
    printf("  \"timers\": {\n");
    printf("    \"total_timers\": %d,\n", total);
    printf("    \"warmup_runs\": %d,\n", warmup);
    printf("    \"measured_runs\": %d,\n", runs);
    printf("    \"wall_ms\": %.1f,\n", med->wall_ms);
    printf("    \"timers_per_sec\": %.0f,\n", med->timers_per_sec);
    print_lat("", &med->lat);
    printf(",\n");
    printf("    \"rss_kb\": %ld\n", (long)get_rss_kb());
    printf("  }");
}

int main(void) {
    int WARMUP = 2, RUNS = 3;
    printf("{\n");
    printf("  \"framework\": \"raw_libuv\",\n");
    printf("  \"system\": { \"cpus\": %ld },\n", sysconf(_SC_NPROCESSORS_ONLN));
    bench_post(1000000, WARMUP, RUNS);
    printf(",\n");
    bench_timers(10000, WARMUP, RUNS);
    printf("\n}\n");
    return 0;
}
