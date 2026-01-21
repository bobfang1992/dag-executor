# Visualizer Step 01: Live Plan Editor with In-Browser Compilation

## Summary

Add interactive TypeScript editor panel to visualizer that compiles plans on-the-fly using QuickJS (WASM) and immediately visualizes the result. Zero backend required.

## Goals

- Edit `.plan.ts` in browser → compile → visualize in real-time
- Reuse existing `quickjs-emscripten` + `esbuild` infrastructure
- No server-side compilation needed
- Provide useful error feedback on compile failures

## Non-Goals

- Full IDE features (autocomplete, go-to-definition) - future enhancement
- Saving plans to filesystem - browser-only for now
- Fragment compilation - plans only initially

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Visualizer App                                                 │
│                                                                 │
│  ┌─────────────────────┐     ┌────────────────────────────────┐│
│  │  Editor Panel       │     │  Canvas Panel (existing)       ││
│  │  ┌───────────────┐  │     │  ┌──────────────────────────┐  ││
│  │  │ Monaco Editor │  │     │  │  PixiJS DAG Renderer     │  ││
│  │  │ (TypeScript)  │  │     │  │                          │  ││
│  │  └───────┬───────┘  │     │  └──────────────────────────┘  ││
│  │          │          │     │              ▲                  ││
│  │  ┌───────▼───────┐  │     │              │                  ││
│  │  │ Compile Btn   │  │     │              │                  ││
│  │  └───────┬───────┘  │     │              │                  ││
│  └──────────┼──────────┘     └──────────────┼──────────────────┘│
│             │                               │                   │
│             ▼                               │                   │
│  ┌──────────────────────────────────────────┘                  │
│  │  Browser Compiler (Web Worker)                              │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │  │ esbuild     │→ │ QuickJS     │→ │ Validator   │→ JSON   │
│  │  │ (WASM)      │  │ (WASM)      │  │             │         │
│  │  └─────────────┘  └─────────────┘  └─────────────┘         │
│  └─────────────────────────────────────────────────────────────┘
└─────────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Browser Compiler Package

**New package:** `dsl/packages/compiler-browser/`

#### 1.1 Abstract Node APIs

Create platform-agnostic interfaces:

```typescript
// dsl/packages/compiler/src/platform.ts
export interface Platform {
  sha256(data: string): Promise<string>;
  resolvePath(...segments: string[]): string;
  basename(path: string, ext?: string): string;
}

// Node implementation (existing behavior)
export const nodePlatform: Platform = { ... };

// Browser implementation
export const browserPlatform: Platform = {
  async sha256(data) {
    const encoder = new TextEncoder();
    const hashBuffer = await crypto.subtle.digest('SHA-256', encoder.encode(data));
    return Array.from(new Uint8Array(hashBuffer))
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
  },
  resolvePath: (...segs) => segs.join('/'),
  basename: (p, ext) => { /* simple impl */ },
};
```

#### 1.2 In-Memory Bundler

Adapt `bundler.ts` for browser:

```typescript
// dsl/packages/compiler-browser/src/bundler.ts
import * as esbuild from 'esbuild-wasm';

// Pre-bundle runtime + generated as virtual modules
const VIRTUAL_MODULES = {
  '@ranking-dsl/runtime': RUNTIME_SOURCE,      // inline at build time
  '@ranking-dsl/generated': GENERATED_SOURCE,  // inline at build time
};

export async function bundleInBrowser(planCode: string, planName: string): Promise<string> {
  await esbuild.initialize({ wasmURL: '/esbuild.wasm' });

  const result = await esbuild.build({
    stdin: {
      contents: planCode,
      loader: 'ts',
      resolveDir: '/',
      sourcefile: `${planName}.plan.ts`,
    },
    bundle: true,
    format: 'iife',
    platform: 'neutral',
    write: false,
    plugins: [virtualModulesPlugin(VIRTUAL_MODULES)],
  });

  return result.outputFiles[0].text;
}
```

#### 1.3 Browser Executor

Adapt `executor.ts`:

```typescript
// dsl/packages/compiler-browser/src/executor.ts
import { getQuickJS } from 'quickjs-emscripten';

export async function executeInBrowser(
  bundledCode: string,
  planName: string
): Promise<PlanArtifact> {
  const QuickJS = await getQuickJS();
  const vm = QuickJS.newContext();

  // Same sandbox setup as Node version
  // Inject __emitPlan, disable eval/Function, etc.

  // Execute and capture artifact
  // ...

  return artifact;
}
```

#### 1.4 Public API

```typescript
// dsl/packages/compiler-browser/src/index.ts
export interface CompileResult {
  success: true;
  artifact: PlanArtifact;
} | {
  success: false;
  error: string;
  phase: 'bundle' | 'execute' | 'validate';
}

export async function compilePlan(
  planCode: string,
  planName: string
): Promise<CompileResult>;

// For web worker usage
export async function initCompiler(): Promise<void>;
export function isInitialized(): boolean;
```

#### 1.5 Build Configuration

```typescript
// dsl/packages/compiler-browser/vite.config.ts
export default {
  build: {
    lib: {
      entry: 'src/index.ts',
      formats: ['es'],
      fileName: 'compiler-browser',
    },
    rollupOptions: {
      // Inline runtime + generated sources at build time
    },
  },
};
```

**Deliverables:**
- `dsl/packages/compiler-browser/` package
- Exports `compilePlan()`, `initCompiler()`
- WASM files: `esbuild.wasm`, `quickjs.wasm`
- Works in main thread or web worker

---

### Phase 2: Editor Panel Component

#### 2.1 Add Monaco Editor Dependency

```bash
pnpm -C tools/visualizer add monaco-editor @monaco-editor/react
```

#### 2.2 Editor Panel Component

```typescript
// tools/visualizer/src/components/EditorPanel.tsx
import Editor from '@monaco-editor/react';
import { useStore } from '../state/store';

const DEFAULT_PLAN = `import { definePlan, EP } from '@ranking-dsl/runtime';
// Key, P, coalesce are globals - no import needed!

export default definePlan({
  name: 'my_plan',
  build: (ctx) => {
    const source = ctx.viewer({ endpoint: EP.redis.default }).follow({ endpoint: EP.redis.default, fanout: 100 });
    const scored = source.vm({ outKey: Key.final_score, expr: Key.id * 0.1 });
    return scored.take({ count: 10 });
  },
});
`;

export function EditorPanel() {
  const [code, setCode] = useState(DEFAULT_PLAN);
  const [error, setError] = useState<string | null>(null);
  const [compiling, setCompiling] = useState(false);
  const setPlan = useStore(s => s.setPlan);

  const handleCompile = async () => {
    setCompiling(true);
    setError(null);
    try {
      const result = await compilePlan(code, 'live_plan');
      if (result.success) {
        setPlan(result.artifact);
      } else {
        setError(`${result.phase}: ${result.error}`);
      }
    } finally {
      setCompiling(false);
    }
  };

  return (
    <div className="editor-panel">
      <div className="editor-toolbar">
        <button onClick={handleCompile} disabled={compiling}>
          {compiling ? 'Compiling...' : 'Compile & Visualize'}
        </button>
      </div>
      <Editor
        height="100%"
        language="typescript"
        value={code}
        onChange={(v) => setCode(v ?? '')}
        theme="vs-dark"
        options={{
          minimap: { enabled: false },
          fontSize: 13,
          tabSize: 2,
        }}
      />
      {error && <div className="error-banner">{error}</div>}
    </div>
  );
}
```

#### 2.3 TypeScript Definitions for Editor

Provide type hints for `@ranking-dsl/*`:

```typescript
// tools/visualizer/src/editor/type-defs.ts
// Generate .d.ts content from runtime + generated packages
// Load into Monaco via monaco.languages.typescript.typescriptDefaults.addExtraLib()

export async function setupTypeDefinitions(monaco: Monaco) {
  // Add runtime types
  monaco.languages.typescript.typescriptDefaults.addExtraLib(
    RUNTIME_DTS,
    'file:///node_modules/@ranking-dsl/runtime/index.d.ts'
  );
  // Add generated types (Key, P, Feat)
  monaco.languages.typescript.typescriptDefaults.addExtraLib(
    GENERATED_DTS,
    'file:///node_modules/@ranking-dsl/generated/index.d.ts'
  );
}
```

**Deliverables:**
- `EditorPanel.tsx` component with Monaco
- TypeScript intellisense for DSL APIs
- Compile button with loading/error states

---

### Phase 3: Visualizer Integration

#### 3.1 Layout Update

Modify `App.tsx` to include editor panel:

```typescript
// tools/visualizer/src/App.tsx
export function App() {
  const [mode, setMode] = useState<'view' | 'edit'>('view');

  return (
    <div className="app">
      <Header mode={mode} onModeChange={setMode} />
      <div className="main-content">
        {mode === 'edit' ? (
          <>
            <EditorPanel />      {/* New */}
            <Canvas />
          </>
        ) : (
          <>
            <SourcePanel />      {/* Existing */}
            <Canvas />
          </>
        )}
      </div>
      <Toolbar />
      <DetailsPanel />
    </div>
  );
}
```

#### 3.2 Store Updates

```typescript
// tools/visualizer/src/state/store.ts
interface Store {
  // Existing...
  plan: PlanJson | null;

  // New
  editorCode: string;
  compileError: string | null;
  isCompiling: boolean;

  // Actions
  setPlan: (plan: PlanJson) => void;
  setEditorCode: (code: string) => void;
  compilePlan: () => Promise<void>;
}
```

#### 3.3 Web Worker (Optional but Recommended)

Move compilation off main thread:

```typescript
// tools/visualizer/src/workers/compiler.worker.ts
import { compilePlan, initCompiler } from '@ranking-dsl/compiler-browser';

self.onmessage = async (e) => {
  if (e.data.type === 'init') {
    await initCompiler();
    self.postMessage({ type: 'ready' });
  } else if (e.data.type === 'compile') {
    const result = await compilePlan(e.data.code, e.data.name);
    self.postMessage({ type: 'result', result });
  }
};
```

**Deliverables:**
- Updated App layout with view/edit modes
- Store integration for editor state
- Optional web worker for non-blocking compilation

---

### Phase 4: Polish & Testing

#### 4.1 UX Enhancements

- Keyboard shortcut: `Cmd/Ctrl+Enter` to compile
- Error highlighting in editor (mark line with error)

#### 4.2 Plan Management (localStorage)

- Save current plan to localStorage with name
- List saved plans in dropdown/sidebar
- Load saved plan into editor
- Rename/delete saved plans
- Auto-save draft on edit (restore on reload)

#### 4.3 URL Sharing

- Encode plan source in URL hash (base64 + compression)
- "Copy Link" button generates shareable URL
- On load, check URL hash and populate editor
- Handle large plans gracefully (URL length limits ~2KB)

#### 4.4 Example Templates

Starter templates in dropdown:

```typescript
const TEMPLATES = {
  'Basic': `// Simple plan with vm + take...`,
  'Filter': `// Plan with filter predicate...`,
  'Concat': `// Multi-source concat plan...`,
  'Regex': `// Plan with regex filter...`,
};
```

#### 4.5 Tests

- Unit tests for browser compiler
- Playwright e2e: type plan → compile → verify DAG renders
- Error case tests: syntax error, sandbox violation, invalid plan
- URL sharing round-trip test

**Deliverables:**
- Keyboard shortcuts
- Template selector
- E2E test coverage

---

## File Changes Summary

### New Files

```
dsl/packages/compiler-browser/
├── package.json
├── tsconfig.json
├── vite.config.ts
├── src/
│   ├── index.ts           # Public API
│   ├── bundler.ts         # esbuild WASM bundling
│   ├── executor.ts        # QuickJS execution
│   ├── platform.ts        # Browser platform impl
│   └── virtual-modules.ts # Inline runtime/generated
└── test/
    └── compile.test.ts

tools/visualizer/src/
├── components/
│   └── EditorPanel.tsx    # New editor component
├── editor/
│   └── type-defs.ts       # Monaco type definitions
└── workers/
    └── compiler.worker.ts # Optional web worker
```

### Modified Files

```
tools/visualizer/
├── package.json           # Add monaco-editor, compiler-browser deps
├── src/App.tsx            # Add edit mode, EditorPanel
├── src/state/store.ts     # Add editor state
└── vite.config.ts         # Configure WASM loading

dsl/packages/compiler/src/
├── bundler.ts             # Extract platform-agnostic parts
├── executor.ts            # Extract platform-agnostic parts
└── platform.ts            # New: Platform interface + Node impl
```

---

## Dependencies

| Package | Version | Purpose |
|---------|---------|---------|
| `monaco-editor` | ^0.45 | Code editor |
| `@monaco-editor/react` | ^4.6 | React wrapper |
| `esbuild-wasm` | ^0.19 | Browser bundling |
| `quickjs-emscripten` | ^0.29 | Already used, works in browser |

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| WASM load time (~2-3s cold) | Show loading indicator; cache in IndexedDB |
| Large bundle size (~3MB WASM) | Lazy load compiler on first edit mode |
| Monaco bundle size (~2MB) | Code-split, load only on edit mode |
| No autocomplete for custom keys | Generate .d.ts from registry at build time |

---

## Decisions

1. ~~Auto-compile toggle?~~ → **Explicit compile button only for v1** (auto-compile later)
2. ~~Persist editor state?~~ → **Yes, localStorage + management UI** (list saved plans, rename, delete)
3. ~~Share plans via URL?~~ → **Yes, URL hash encoding** (copy shareable link)
4. ~~Multiple files/fragments?~~ → **Future work** (fragment compiler not implemented yet)

---

## Success Criteria

- [ ] User can type TypeScript plan in editor
- [ ] Compile button produces valid plan.json
- [ ] DAG visualizes immediately after compile
- [ ] Compile errors shown inline with helpful messages
- [ ] TypeScript intellisense works for Key, P, E, Pred APIs
- [ ] No server required - fully client-side
- [ ] Plans persist to localStorage with management UI
- [ ] Shareable URL generates and loads correctly
