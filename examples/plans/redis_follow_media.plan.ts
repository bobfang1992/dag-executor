/**
 * Redis Follow Media Plan - demonstrates Redis-backed tasks
 *
 * Pipeline:
 * 1. follow: Get followees for current user (from Redis follow:{uid})
 * 2. media: For each followee, get their media items (from Redis media:{id})
 * 3. take: Limit to first 50 results
 */

import { definePlan, EP } from "@ranking-dsl/runtime";

export default definePlan({
  name: "redis_follow_media",
  build: (ctx) => {
    // Get followees for the viewer
    const followees = ctx.follow({
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
