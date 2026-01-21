/**
 * Redis Follow Media Plan - demonstrates Redis-backed tasks
 *
 * Pipeline:
 * 1. viewer: Get current user
 * 2. follow: Get followees for viewer (from Redis follow:{uid})
 * 3. media: For each followee, get their media items (from Redis media:{id})
 * 4. take: Limit to first 50 results
 */

import { definePlan } from "@ranking-dsl/runtime";

export default definePlan({
  name: "redis_follow_media",
  build: (ctx) => {
    // Get viewer and their followees
    const followees = ctx.viewer({ endpoint: EP.redis.default }).follow({
      endpoint: EP.redis.default,
      fanout: 10,
      trace: "follow_source",
    });

    // For each followee, get their media
    const media = followees.media({
      endpoint: EP.redis.default,
      fanout: 5,
      trace: "media_expand",
    });

    // Limit to 50 results
    return media.take({ count: 50, trace: "take_50" });
  },
});
