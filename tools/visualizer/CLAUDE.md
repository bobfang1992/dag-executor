# Plan Visualizer

Interactive DAG visualization tool for `*.plan.json` files.

## Tech Stack

- **React 18** - UI framework
- **PixiJS 8** - WebGL canvas rendering (60fps at scale)
- **dockview** - Draggable, resizable panel layout
- **dagre** - Hierarchical DAG layout
- **Zustand** - State management
- **Vite** - Build tooling
- **Dracula** - Color theme

## Commands

```bash
# Development
pnpm run visualizer:dev      # Start dev server (http://localhost:5175)

# Build
pnpm run visualizer:build    # Production build → tools/visualizer/dist/
pnpm run visualizer:preview  # Preview production build

# Testing
pnpm -C tools/visualizer run test          # Run Playwright e2e tests
pnpm -C tools/visualizer run test:ui       # Run tests with Playwright UI
pnpm -C tools/visualizer run test:headed   # Run tests in headed browser
```

**IMPORTANT:** Visualizer e2e tests are NOT run in CI. Always run `pnpm -C tools/visualizer run test` locally before committing changes to visualizer files.

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

### Step 01: Live Plan Editor ✅

**Phase 1: Server-Side Compiler** ✅
- Compilation via Vite dev server API (`/api/compile`)
- Uses real `dslc` CLI for full parity with production
- AST extraction for natural expression syntax (`Key.x * P.y`)

**Phase 2: Monaco Editor** ✅
- Full TypeScript editor with syntax highlighting
- DSL type definitions for intellisense (generated from registries)
- Auto-complete for `definePlan`, `Key`, `P`, task methods, etc.
- Suppresses TypeScript errors for natural expression/predicate syntax (2304, 2362, 2363, 2322, 2365, 2367)

**Phase 3: Persistence & Sharing** ✅
- Auto-save to localStorage (debounced)
- URL hash encoding for shareable links
- Saved plans management (save as, rename, delete)

**Phase 4: Polish** ✅
- Resizable editor panel (draggable divider)
- Keyboard shortcuts (⌘+Enter compile, ⌘+S save, ⌘+Shift+F format)
- Reusable UI components (Button, Dropdown, Modal)
- E2E tests with Playwright

### Step 02: Dockview Panel Layout ✅

**Dockview Integration**
- Replaced manual flex+divider layout with dockview
- Draggable, resizable, snappable panel windows
- Panels: Editor, Canvas, Details, Source

**Menu Bar**
- "Add" menu to restore closed panels
- "View" menu with Reset Layout option
- Panels restore to their previous position when re-added

**Persistence**
- Layout persists across page refreshes and sessions
- Separate layouts for view mode vs edit mode
- Centralized preferences module for all localStorage

**Polish**
- DAG-style favicon (nodes + edges in dracula colors)
- Simple home page when no plan loaded
- E2E tests for panel interactions

### Step 03: Registry Viewer & Edit Existing Plan ✅

**Registry Viewer**
- Browse Keys, Params, Features, Capabilities, Tasks
- Tabbed interface with search/filter
- Status badges (active=green, deprecated=yellow, blocked=red)
- Clickable cross-references in Details panel (`Key[3001]` → registry entry)
- Multiple draggable detail panels
- URL persistence (`?view=registries&tab=keys&selected=3001`)

**Edit Existing Plan**
- "Edit" button on plan cards in home page
- "Edit" button in canvas toolbar when viewing a plan
- Loads plan source into editor for modification
- Only shows Edit in view mode (hidden in edit mode)

**Navbar**
- Editor / Plans / Registries navigation tabs
- Active tab highlighted
- Replaces "Back to Home" button

**Polish**
- Project folder indicator in header
- Fixed Fit button to use container dimensions
- E2E tests for edit plan flow

### Step 04: Fragment Support 🔲

- Detect fragment boundaries in plan
- Render fragments as collapsible supernodes
- Click to expand/collapse fragment
- Auto-collapse when visible nodes > threshold
- LRU eviction for expanded fragments

### Step 05: Enhanced UX 🔲

- Keyboard shortcuts (Escape=deselect, +/-=zoom, F=fit)
- Minimap for large graphs
- Search/filter nodes
- Export as PNG/SVG

### Step 06: Production 🔲

- Production build optimization
- Deploy to GitHub Pages or similar
- CI integration for artifact updates

## Architecture

```
tools/visualizer/
├── public/
│   └── favicon.svg           # DAG-style favicon
├── src/
│   ├── main.tsx              # Entry point + dockview CSS
│   ├── App.tsx               # Layout: header + menu bar + DockLayout
│   ├── theme.ts              # Dracula color palette
│   ├── types.ts              # VisNode, VisEdge, PlanJson interfaces
│   ├── components/
│   │   ├── Canvas.tsx        # PixiJS canvas with pan/zoom
│   │   ├── DetailsPanel.tsx  # Node details + expr/pred formatting
│   │   ├── DockLayout.tsx    # Dockview wrapper + layout persistence
│   │   ├── Dropdown.tsx      # Custom styled dropdown
│   │   ├── EditorPanel.tsx   # Monaco editor with live compilation
│   │   ├── MenuBar.tsx       # Add/View dropdown menus
│   │   ├── Modal.tsx         # PromptModal and ConfirmModal
│   │   ├── PlanSelector.tsx  # Plan list from index.json + drag-drop + edit buttons
│   │   ├── RegistryViewer.tsx # Registry browser (keys, params, features, etc.)
│   │   ├── SourcePanel.tsx   # Original .plan.ts source viewer
│   │   ├── Toolbar.tsx       # Floating toolbar (stats + fit + edit buttons)
│   │   └── ui/
│   │       └── Button.tsx    # Reusable button component
│   ├── layout/
│   │   └── dagre-layout.ts   # DAG layout using dagre
│   ├── parser/
│   │   └── plan-parser.ts    # plan.json → VisGraph + node color logic
│   └── state/
│       ├── store.ts          # Zustand store + browser history
│       └── preferences.ts    # Centralized localStorage module
├── e2e/
│   ├── editor.spec.ts        # Editor e2e tests
│   ├── dockview.spec.ts      # Panel layout e2e tests
│   └── edit-plan.spec.ts     # Edit existing plan + navbar tests
├── playwright.config.ts      # Playwright configuration
├── vite.config.ts            # Vite config + middlewares
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

### Panel Layout (Dockview)
- Draggable, resizable panel windows (like VS Code or Compiler Explorer)
- Panels: Editor, Canvas, Details, Source
- Close panels via tab × button
- Restore panels via Add menu (restores to previous position)
- Reset Layout via View menu
- Layout persists across sessions (separate for view/edit modes)

### Source Viewer
- Shows original `.plan.ts` source with line numbers
- Served via Vite middleware from `plans/` or `examples/plans/`
- Available as dockview panel in view mode

### Canvas Toolbar
- Floating toolbar at bottom center of canvas
- Shows node/edge count
- Fit button to auto-fit graph to view
- Edit button (view mode only) to edit current plan's source

### Edit Existing Plan
- **From home page**: Click "Edit" button on any plan card
- **From plan view**: Click "Edit" button in canvas toolbar
- Loads the plan's `.plan.ts` source into the editor
- Edit button only appears when source is available
- Edit button hidden in edit mode (already editing)

### Registry Viewer
- Access via "Registries" nav tab or "Browse Registries" card
- Tabbed interface: Keys, Params, Features, Capabilities, Tasks
- Color-coded tabs (blue=keys, orange=params, green=features, etc.)
- Search/filter across name and documentation
- Status badges: active (green), deprecated (yellow), blocked (red)
- Click row to open draggable detail panel
- Multiple detail panels can be open simultaneously
- Cross-references from Details panel (`Key[3001]`) link to registry

### Navbar
- Three tabs: Editor, Plans, Registries
- Active tab highlighted
- Editor: Opens live plan editor
- Plans: Shows plan selector (home page)
- Registries: Opens registry viewer

### Live Plan Editor (Step 01)
- Click "Create New Plan" to open editor panel
- Monaco editor with TypeScript support and DSL intellisense
- Click "Compile & Visualize" or press ⌘+Enter to compile
- Server-side compilation using real `dslc` CLI (full parity with production)
- Natural expression syntax supported (`Key.x * P.y`, `coalesce()`)
- Save plans to localStorage with "Save As" (⌘+S)
- Share plans via URL hash encoding
- Manage saved plans: rename, delete, switch between them

### Menu Bar
- **Add**: Restore closed panels (Editor, Canvas, Details, Source)
- **View**: Reset Layout to default arrangement
- Items disabled when panel already exists or not applicable to current mode

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

## Compilation Architecture

The live editor uses **server-side compilation** for full parity with the production `dslc` compiler.

```
┌─────────────────────┐                              ┌──────────────────────┐
│   Monaco Editor     │    POST /api/compile         │   Vite Dev Server    │
│   (browser)         │  ───────────────────────►    │                      │
│                     │    { source, filename }      │   ┌────────────────┐ │
│                     │                              │   │  dslc CLI      │ │
│                     │  ◄───────────────────────    │   │  (QuickJS +    │ │
│                     │    { artifact } or           │   │   esbuild +    │ │
└─────────────────────┘    { error, phase }          │   │   AST extract) │ │
                                                     │   └────────────────┘ │
                                                     └──────────────────────┘
```

**Why server-side?**
- **Full parity**: Same compiler as production (AST extraction, all validations)
- **Natural expressions**: `Key.x * P.y` syntax works correctly via AST extraction
- **Smaller bundle**: No QuickJS WASM or esbuild-wasm in browser (~15MB saved)
- **Single compiler path**: One codebase to maintain

**API Endpoint** (`/api/compile`):
1. Receives `{ source: string, filename: string }`
2. Writes source to temp file
3. Spawns `node dsl/packages/compiler/dist/cli.js build <file> --out <dir>`
4. Returns `{ success: true, artifact }` or `{ success: false, error, phase }`
5. Cleans up temp files

**Monaco Type Definitions**:
- Generated from registries (`dsl/packages/generated/monaco-types.ts`)
- Includes all Keys, Params, and task methods from `tasks.toml`
- Suppresses TypeScript errors 2304/2362/2363/2322/2365/2367 for globals and natural expression/predicate syntax

## Vite Config Notes

- `publicDir` points to `../../artifacts` to serve plan JSONs at `/plans/`
- `serveLocalPublic()` middleware serves favicon and other local assets from `public/`
- `servePlanSources()` middleware serves `.plan.ts` sources at `/sources/<name>.plan.ts`
- Searches both `plans/` and `examples/plans/` directories
- `/api/compile` endpoint for server-side plan compilation (uses real `dslc` CLI)
- **Security note**: The middlewares don't validate paths. This is acceptable since the visualizer is a local dev tool only (not deployed to production).

## Screenshots

```bash
# Local screenshot capture
pnpm -C tools/visualizer exec playwright install chromium  # First time only
pnpm -C tools/visualizer run screenshots
```

Screenshots saved to `tools/visualizer/screenshots/` (gitignored).
