---
rfc: 0006
title: "Plan Visualizer (Web Tool)"
status: Draft
created: 2026-01-15
updated: 2026-01-15
authors:
  - "TBD"
approvers: []
requires: []
replaces: []
capability_id: "cap.rfc.0006.plan-visualizer.v1"
---

# RFC 0006: Plan Visualizer (Web Tool)

## 0. Summary

A web-based DAG visualizer for compiled plans (and later fragments). Fragments render as collapsible supernodes. Auto-collapses fragments when visible node count exceeds threshold. Built with React + a GPU-accelerated canvas renderer for snappy interaction at scale.

## 1. Motivation

- Plans can grow to 1000+ nodes; JSON is unreadable for debugging.
- Critical path analysis (spec §11.5) needs visual highlighting.
- Fragment boundaries obscured after linking; need expand/collapse.
- Existing tools (Graphviz, Mermaid) are static or slow at scale.

## 2. Goals

- **G1**: Render plan JSON as interactive DAG (pan, zoom, select).
- **G2**: Fragment supernodes: collapsed by default, expand on click.
- **G3**: Auto-collapse fragments when total visible nodes > threshold.
- **G4**: Snappy UX: 60fps pan/zoom on 1000-node graphs.
- **G5**: Minimal MVP; extensible for future features (trace overlay, editing).

## 3. Non-Goals (MVP)

- **NG1**: Fragment authoring/editing in the tool.
- **NG2**: Live engine integration (trace streaming).
- **NG3**: Diff view between plan versions.
- **NG4**: Mobile support.

## 4. Background / Prior Art

| Tool | Pros | Cons |
|------|------|------|
| Graphviz/dot | Mature layout algos | Static, no interactivity |
| Mermaid | Markdown-friendly | Slow >100 nodes |
| D3.js | Flexible, SVG-based | SVG perf ceiling ~500 nodes |
| Cytoscape.js | Graph-specific API | Heavy bundle, complex API |
| React Flow | React-native, good DX | SVG-based, struggles >1k nodes |
| PixiJS | WebGL, 60fps at scale | Lower-level, manual layout |
| Sigma.js | WebGL graph viz | Less flexible for custom nodes |

## 5. Proposal (high level)

Ship a standalone web app (`tools/visualizer/`) that:
1. Loads `*.plan.json` (drag-drop or file picker).
2. Parses nodes, edges, fragment boundaries.
3. Renders DAG with collapsible fragment supernodes.
4. Auto-layout via dagre/ELK (server-side or WASM).

## 6. Detailed Design

### 6.1 Tech Stack

| Layer | Choice | Rationale |
|-------|--------|-----------|
| Framework | React 18 | Team familiarity, component model |
| Renderer | **PixiJS 8** | WebGL perf, handles 10k+ sprites |
| Layout | dagre or ELK.js (WASM) | Hierarchical DAG layout |
| State | Zustand or Jotai | Minimal, performant |
| Build | Vite | Fast dev, tree-shaking |

**Renderer comparison (detailed):**

| Renderer | Nodes @ 60fps | Bundle | Custom shapes | Learning curve |
|----------|---------------|--------|---------------|----------------|
| **PixiJS** | 10k+ | 300KB | Full control | Medium |
| React Flow | ~500 | 150KB | Easy (React) | Low |
| Sigma.js | 5k+ | 200KB | Limited | Medium |
| Three.js | 50k+ | 600KB | Overkill for 2D | High |
| Raw Canvas2D | 2k | 0 | Manual | Medium |

**Recommendation**: PixiJS for MVP. Migrate to React Flow if graphs stay small (<500 nodes).

### 6.2 Data Model

```typescript
interface VisNode {
  id: string;
  op: string;
  label: string;
  fragment?: string;       // origin fragment name
  fragmentVersion?: string;
  isFragment: boolean;     // true = collapsed supernode
  children?: string[];     // node IDs inside (when collapsed)
  x: number; y: number;    // layout coords
  expanded: boolean;
}

interface VisEdge {
  from: string;
  to: string;
  hidden: boolean;  // true when edge crosses collapsed boundary
}

interface VisState {
  nodes: Map<string, VisNode>;
  edges: VisEdge[];
  expandedFragments: Set<string>;
  visibleNodeCount: number;
  autoCollapseThreshold: number; // default 200
}
```

### 6.3 Fragment Collapse Logic

```
on expandFragment(fragmentId):
  add fragmentId to expandedFragments
  recompute visibleNodeCount
  if visibleNodeCount > autoCollapseThreshold:
    collapse oldest expanded fragments until under threshold
  re-layout affected subgraph
```

"Oldest" = LRU order of expansion.

### 6.4 Layout Strategy

1. **Initial load**: run dagre on collapsed graph (fragments as single nodes).
2. **On expand**: incremental layout for fragment internals; anchor parent position.
3. **On collapse**: remove internal nodes, restore supernode position.

### 6.5 Interaction

| Action | Behavior |
|--------|----------|
| Click node | Select, show details panel |
| Double-click fragment | Expand/collapse |
| Pan | Middle-drag or two-finger |
| Zoom | Scroll wheel, pinch |
| Hover | Highlight incoming/outgoing edges |

### 6.6 File Structure (MVP)

```
tools/visualizer/
  package.json
  vite.config.ts
  index.html
  src/
    main.tsx
    App.tsx
    components/
      Canvas.tsx          # PixiJS wrapper
      NodeSprite.tsx      # Node rendering
      DetailsPanel.tsx    # Selected node info
    state/
      store.ts            # Zustand store
    layout/
      dagre-layout.ts
    parser/
      plan-parser.ts      # plan.json → VisState
```

### 6.7 Observability / Debuggability

- Console logs layout timing.
- FPS counter in dev mode.
- Node count badge in UI.

### 6.8 Security

- No network requests; local file only.
- No eval; JSON.parse only.
- CSP: `default-src 'self'`.

## 7. Backward Compatibility

Pure additive tooling; no spec.md or engine changes.

## 8. Alternatives Considered

1. **Graphviz CLI + static HTML**: rejected; no interactivity.
2. **React Flow only**: rejected; SVG perf ceiling.
3. **Electron app**: rejected; web is more accessible.
4. **VS Code extension**: future possibility; web-first is simpler MVP.

## 9. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| PixiJS learning curve | Start with simple rect+text sprites |
| Layout perf on huge graphs | WASM ELK, incremental layout |
| Scope creep | Strict MVP; defer trace/edit features |

## 10. Test Plan

- Unit: parser extracts nodes/edges correctly.
- Visual: screenshot regression for sample plans.
- Perf: measure FPS on 500, 1000, 2000 node plans.

## 11. Rollout Plan

1. MVP: local file load, collapse/expand, pan/zoom.
2. V1: URL param to load from artifacts dir (dev server).
3. V2: Trace overlay (critical path highlighting).
4. V3: Fragment diff view.

## 12. Open Questions

- **Q1**: Should auto-collapse threshold be configurable in UI or hardcoded?
- **Q2**: Persist expand/collapse state in localStorage?
- **Q3**: Support fragment-only JSON files, or plan-only for MVP?
- **Q4**: Dark mode from day 1 or defer?

---

## Appendix A: Mockup

```
┌─────────────────────────────────────────────────────┐
│  Plan Visualizer          [Upload] [Fit] [Reset]   │
├─────────────────────────────────────────────────────┤
│                                                     │
│    ┌──────┐      ┌──────────────┐      ┌──────┐    │
│    │source│─────▶│ ▶ esr (v1)   │─────▶│ take │    │
│    └──────┘      │   [3 nodes]  │      └──────┘    │
│                  └──────────────┘                   │
│                         │                          │
│                         ▼                          │
│                  ┌──────────────┐                   │
│                  │   vm_final   │                   │
│                  └──────────────┘                   │
│                                                     │
├─────────────────────────────────────────────────────┤
│ Selected: esr (v1)  │ op: fragment │ nodes: 3      │
└─────────────────────────────────────────────────────┘
```

## Appendix B: Future Extensions (not in MVP)

- Trace overlay: color nodes by latency, highlight critical path.
- Search/filter nodes by op, key, param.
- Side-by-side diff of two plan versions.
- Export to PNG/SVG.
- Inline editing (param values, connections).
- Integration with `dslc` dev server (hot reload).
