# Plan Visualizer

Interactive DAG visualization tool for `*.plan.json` files.

## Tech Stack

- **React 18** - UI framework
- **PixiJS 8** - WebGL canvas rendering (60fps at scale)
- **dagre** - Hierarchical DAG layout
- **Zustand** - State management
- **Vite** - Build tooling
- **US Graphics** - Color theme (light, Univers font)

## Commands

```bash
# Development
pnpm run visualizer:dev      # Start dev server (http://localhost:5173)

# Build
pnpm run visualizer:build    # Production build → tools/visualizer/dist/
pnpm run visualizer:preview  # Preview production build
```

## Implementation Phases

### Step 00: MVP (Current) ✅

**Phase 1: Project Setup** ✅
- Workspace config (pnpm-workspace.yaml, package.json)
- Vite + React + TypeScript setup
- Basic app shell (index.html, main.tsx, App.tsx)
- Type definitions (types.ts)

**Phase 2: Plan Loading** ✅
- Auto-load plans from `artifacts/plans/index.json`
- Drag-drop and file picker for local files
- Zustand store for state management
- Browser history support (back button, URL params)

**Phase 3: DAG Rendering** ✅
- Plan parser (plan.json → VisGraph)
- dagre layout (hierarchical positioning)
- PixiJS canvas with pan/zoom
- Node colors by operation type
- Curved bezier edges with arrowheads

**Phase 4: Interactivity** ✅
- Node selection (click)
- Hover highlighting (dim unconnected nodes)
- Details panel (node info, params, expr/pred)
- Source panel with draggable divider
- macOS dock-style toolbar

**Phase 5: Polish** ✅
- US Graphics theme (light, clean)
- Clickable title to go back
- Fit/Reset view buttons

### Step 01: Fragment Support (Next) 🔲

- Detect fragment boundaries in plan
- Render fragments as collapsible supernodes
- Click to expand/collapse fragment
- Auto-collapse when visible nodes > threshold
- LRU eviction for expanded fragments

### Step 02: Enhanced UX 🔲

- Keyboard shortcuts (Escape=deselect, +/-=zoom, F=fit)
- localStorage for view preferences
- Minimap for large graphs
- Search/filter nodes
- Export as PNG/SVG

### Step 03: Production 🔲

- Production build optimization
- Deploy to GitHub Pages or similar
- CI integration for artifact updates

## Architecture

```
tools/visualizer/
├── src/
│   ├── main.tsx              # Entry point
│   ├── App.tsx               # Layout: header + source panel + canvas + details
│   ├── theme.ts              # US Graphics color palette
│   ├── types.ts              # VisNode, VisEdge, PlanJson interfaces
│   ├── components/
│   │   ├── Canvas.tsx        # PixiJS canvas with pan/zoom
│   │   ├── DetailsPanel.tsx  # Node details + expr/pred formatting
│   │   ├── PlanSelector.tsx  # Plan list from index.json + drag-drop
│   │   ├── SourcePanel.tsx   # Original .plan.ts source viewer
│   │   └── Toolbar.tsx       # Dock-style toolbar
│   ├── layout/
│   │   └── dagre-layout.ts   # DAG layout using dagre
│   ├── parser/
│   │   └── plan-parser.ts    # plan.json → VisGraph + node color logic
│   └── state/
│       └── store.ts          # Zustand store + browser history
├── vite.config.ts            # Vite config + source file middleware
└── index.html
```

## Features

### Plan Loading
- Auto-loads available plans from `artifacts/plans/index.json`
- Drag-drop or file picker for local `.plan.json` files
- Validates `schema_version` and required fields
- Browser history support (back button works, URL includes `?plan=<name>`)

### DAG Rendering
- Nodes auto-sized to fit text (op name + trace label)
- Color-coded by operation type (see Node Colors below)
- Curved bezier edges with arrowheads
- Pan (drag) and zoom (wheel, clamped 0.1x–4x)

### Node Colors

| Operation | Color | Hex |
|-----------|-------|-----|
| Output nodes | Red | `#ff0037` |
| `viewer.*` (sources) | Blue | `#0092ff` |
| `concat` (composition) | Pink | `#ff41b4` |
| `vm` (expression) | Purple | `#8969ff` |
| `filter` | Orange | `#ffb700` |
| `take` | Green | `#00c986` |
| Default | Gray | `#666666` |

### Interactivity
- Click node → select (yellow highlight border)
- Hover node → highlight connected edges, dim unconnected nodes
- Details panel shows: node_id, op, params, inputs/outputs
- Expr/Pred formatting for vm and filter nodes
- Click "Plan Visualizer" title → go back to plan selector

### Source Viewer
- Shows original `.plan.ts` source with line numbers
- Served via Vite middleware from `plans/` or `examples/plans/`
- Draggable divider to resize source/graph split (20%-80% range)
- Toggle with "Source" button in dock

### Dock Toolbar
- macOS-style floating dock at bottom center
- Blur backdrop effect
- Buttons: Source toggle, Fit, Back

## Data Flow

```
index.json → PlanSelector → loadPlanByName()
                              ↓
                         fetch plan.json
                              ↓
                         parsePlan() → VisGraph
                              ↓
                         dagreLayout() → nodes with x,y
                              ↓
                         Canvas renders via PixiJS
```

## Vite Config Notes

- `publicDir` points to `../../artifacts` to serve plan JSONs at `/plans/`
- Custom middleware serves `.plan.ts` sources at `/sources/<name>.plan.ts`
- Searches both `plans/` and `examples/plans/` directories

## CI Screenshots

Automated screenshot capture on PRs touching `tools/visualizer/**`:

```bash
# Local screenshot capture
pnpm -C tools/visualizer exec playwright install chromium  # First time only
pnpm run visualizer:build
pnpm -C tools/visualizer run screenshots
```

Screenshots saved to `tools/visualizer/screenshots/` (gitignored).

GitHub Actions workflow (`.github/workflows/visualizer-screenshots.yml`):
1. Builds visualizer
2. Captures screenshots via Playwright
3. Uploads as artifacts
4. Posts summary comment on PR
