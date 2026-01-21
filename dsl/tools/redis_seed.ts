/**
 * Redis Seed Script - Deterministic test data for local development
 *
 * Usage:
 *   pnpm -C dsl tsx tools/redis_seed.ts
 *
 * Environment variables:
 *   REDIS_HOST - Redis host (default: 127.0.0.1)
 *   REDIS_PORT - Redis port (default: 6379)
 *   REDIS_DB   - Redis database (default: 0)
 */

import { createClient } from "redis";

// =====================================================
// Constants (must be stable for Step 14.4 tasks)
// =====================================================

const NUM_USERS = 20;
const FOLLOW_FANOUT = 5;
const MEDIA_PER_USER = 10;

const COUNTRIES = ["US", "CA", "GB"] as const;

// =====================================================
// Deterministic data generation
// =====================================================

function getCountry(uid: number): string {
  return COUNTRIES[(uid - 1) % 3];
}

function getFollowees(uid: number): string[] {
  const followees: string[] = [];
  for (let i = 1; i <= FOLLOW_FANOUT; i++) {
    const followee = ((uid + i - 1) % NUM_USERS) + 1;
    followees.push(String(followee));
  }
  return followees;
}

function getMediaIds(uid: number): string[] {
  const mediaIds: string[] = [];
  for (let j = 1; j <= MEDIA_PER_USER; j++) {
    mediaIds.push(String(uid * 1000 + j));
  }
  return mediaIds;
}

// =====================================================
// Main
// =====================================================

async function main(): Promise<void> {
  const host = process.env.REDIS_HOST ?? "127.0.0.1";
  const port = parseInt(process.env.REDIS_PORT ?? "6379", 10);
  const db = parseInt(process.env.REDIS_DB ?? "0", 10);

  console.log(`Connecting to Redis at ${host}:${port} (db=${db})...`);

  const client = createClient({
    socket: { host, port },
    database: db,
  });

  client.on("error", (err) => {
    console.error("Redis client error:", err);
  });

  await client.connect();

  try {
    // Always flush for local harness
    console.log("Flushing database...");
    await client.flushDb();

    // Seed users
    console.log(`Seeding ${NUM_USERS} users...`);
    for (let uid = 1; uid <= NUM_USERS; uid++) {
      await client.hSet(`user:${uid}`, {
        id: String(uid),
        country: getCountry(uid),
      });
    }

    // Seed follow fanout
    console.log(`Seeding follow lists (${FOLLOW_FANOUT} per user)...`);
    for (let uid = 1; uid <= NUM_USERS; uid++) {
      const followees = getFollowees(uid);
      await client.rPush(`follow:${uid}`, followees);
    }

    // Seed media fanout
    console.log(`Seeding media lists (${MEDIA_PER_USER} per user)...`);
    for (let uid = 1; uid <= NUM_USERS; uid++) {
      const mediaIds = getMediaIds(uid);
      await client.rPush(`media:${uid}`, mediaIds);
    }

    // =====================================================
    // Smoke checks
    // =====================================================
    console.log("\nRunning smoke checks...");

    // Check user:1 country exists
    const user1Country = await client.hGet("user:1", "country");
    if (user1Country !== "US") {
      throw new Error(
        `Smoke check failed: user:1 country expected "US", got "${user1Country}"`
      );
    }
    console.log("  [OK] user:1 country = US");

    // Check follow:1 length
    const follow1Len = await client.lLen("follow:1");
    if (follow1Len !== FOLLOW_FANOUT) {
      throw new Error(
        `Smoke check failed: follow:1 length expected ${FOLLOW_FANOUT}, got ${follow1Len}`
      );
    }
    console.log(`  [OK] follow:1 length = ${FOLLOW_FANOUT}`);

    // Check follow:1 contents
    const follow1 = await client.lRange("follow:1", 0, -1);
    const expectedFollow1 = getFollowees(1);
    if (JSON.stringify(follow1) !== JSON.stringify(expectedFollow1)) {
      throw new Error(
        `Smoke check failed: follow:1 expected ${JSON.stringify(expectedFollow1)}, got ${JSON.stringify(follow1)}`
      );
    }
    console.log(`  [OK] follow:1 contents = ${JSON.stringify(follow1)}`);

    // Check media:1 length
    const media1Len = await client.lLen("media:1");
    if (media1Len !== MEDIA_PER_USER) {
      throw new Error(
        `Smoke check failed: media:1 length expected ${MEDIA_PER_USER}, got ${media1Len}`
      );
    }
    console.log(`  [OK] media:1 length = ${MEDIA_PER_USER}`);

    console.log("\nSeed complete. All smoke checks passed.");
  } finally {
    await client.quit();
  }
}

main().catch((err) => {
  console.error("Seed failed:", err);
  process.exit(1);
});
