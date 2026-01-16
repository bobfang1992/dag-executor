import { useState, useCallback, useRef, useEffect } from 'react';
import Editor, { OnMount } from '@monaco-editor/react';
import type { editor } from 'monaco-editor';
import { useStore } from '../state/store';
import { dracula } from '../theme';

const DEFAULT_PLAN = `import { definePlan } from '@ranking-dsl/runtime';
import { Key, P } from '@ranking-dsl/generated';

export default definePlan({
  name: 'my_plan',
  build: (c, ctx) => {
    // Source: fetch viewer's followed accounts
    const source = ctx.viewer.follow({ fanout: 100 });

    // Score: compute final_score using vm expression
    const scored = source.vm(
      Key.final_score,
      Key.model_score_1 + Key.model_score_2 * P.media_age_penalty_weight
    );

    // Filter: keep only high-scoring items
    const filtered = scored.filter(Key.final_score > 0.5);

    // Return top 10
    return filtered.take({ limit: 10 });
  },
});
`;

// Type definitions for DSL (embedded for Monaco intellisense)
const DSL_TYPES = `
declare module '@ranking-dsl/runtime' {
  export interface PlanContext {
    viewer: {
      follow(opts: { fanout: number; trace?: string }): CandidateSet;
      fetch_cached_recommendation(opts: { fanout: number; trace?: string }): CandidateSet;
    };
    requireCapability(capId: string, payload?: unknown): void;
  }

  export interface CandidateSet {
    vm(targetKey: KeyRef, expr: ExprNode, opts?: { trace?: string }): CandidateSet;
    filter(pred: PredNode, opts?: { trace?: string }): CandidateSet;
    take(opts: { limit: number; trace?: string }): CandidateSet;
    concat(other: CandidateSet, opts?: { trace?: string }): CandidateSet;
  }

  export interface PlanConfig {
    name: string;
    build: (c: PlanCtx, ctx: PlanContext) => CandidateSet;
  }

  export function definePlan(config: PlanConfig): void;

  export type KeyRef = { __brand: 'key' } & number;
  export type ParamRef = { __brand: 'param' } & number;
  export type ExprNode = KeyRef | ParamRef | number;
  export type PredNode = { __brand: 'pred' };
  export type PlanCtx = {};
}

declare module '@ranking-dsl/generated' {
  import { KeyRef, ParamRef } from '@ranking-dsl/runtime';

  export const Key: {
    readonly id: KeyRef;
    readonly model_score_1: KeyRef;
    readonly model_score_2: KeyRef;
    readonly final_score: KeyRef;
    readonly country: KeyRef;
    readonly title: KeyRef;
    readonly features_esr: KeyRef;
    readonly features_lsr: KeyRef;
  };

  export const P: {
    readonly media_age_penalty_weight: ParamRef;
    readonly blocklist_regex: ParamRef;
    readonly esr_cutoff: ParamRef;
  };
}
`;

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    flexDirection: 'column',
    height: '100%',
    background: dracula.background,
  },
  toolbar: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '8px 12px',
    background: dracula.headerBg,
    borderBottom: `1px solid ${dracula.border}`,
  },
  button: {
    background: dracula.purple,
    color: dracula.foreground,
    border: 'none',
    padding: '6px 14px',
    borderRadius: '4px',
    cursor: 'pointer',
    fontSize: '13px',
    fontWeight: 500,
  },
  buttonDisabled: {
    background: dracula.currentLine,
    cursor: 'not-allowed',
  },
  status: {
    fontSize: '12px',
    color: dracula.comment,
  },
  statusError: {
    color: dracula.red,
  },
  statusSuccess: {
    color: dracula.green,
  },
  editorContainer: {
    flex: 1,
    minHeight: 0,
  },
  errorBanner: {
    padding: '8px 12px',
    background: `${dracula.red}22`,
    borderTop: `1px solid ${dracula.red}`,
    color: dracula.red,
    fontSize: '12px',
    fontFamily: 'ui-monospace, monospace',
    whiteSpace: 'pre-wrap',
    maxHeight: '100px',
    overflow: 'auto',
  },
};

export default function EditorPanel() {
  const [code, setCode] = useState(DEFAULT_PLAN);
  const [compiling, setCompiling] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState<'ready' | 'compiling' | 'success' | 'error'>('ready');
  const [compilerReady, setCompilerReady] = useState(false);

  const editorRef = useRef<editor.IStandaloneCodeEditor | null>(null);
  const compilerRef = useRef<{
    compilePlan: (source: string, name: string) => Promise<unknown>;
  } | null>(null);

  const loadPlan = useStore((s) => s.loadPlan);

  // Initialize compiler
  useEffect(() => {
    async function initCompiler() {
      try {
        setStatus('compiling');
        // Dynamic import to avoid bundling issues
        const compiler = await import('@ranking-dsl/compiler/browser');
        await compiler.initCompiler();
        compilerRef.current = {
          compilePlan: compiler.compilePlan,
        };
        setCompilerReady(true);
        setStatus('ready');
      } catch (err) {
        setError(`Failed to initialize compiler: ${err instanceof Error ? err.message : String(err)}`);
        setStatus('error');
      }
    }
    initCompiler();
  }, []);

  // Setup Monaco with type definitions
  const handleEditorMount: OnMount = useCallback((editor, monaco) => {
    editorRef.current = editor;

    // Configure TypeScript
    monaco.languages.typescript.typescriptDefaults.setCompilerOptions({
      target: monaco.languages.typescript.ScriptTarget.ES2020,
      module: monaco.languages.typescript.ModuleKind.ESNext,
      moduleResolution: monaco.languages.typescript.ModuleResolutionKind.NodeJs,
      allowNonTsExtensions: true,
      strict: true,
    });

    // Add DSL type definitions
    monaco.languages.typescript.typescriptDefaults.addExtraLib(
      DSL_TYPES,
      'file:///node_modules/@ranking-dsl/index.d.ts'
    );

    // Add keyboard shortcut for compile (Cmd/Ctrl+Enter)
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => {
      handleCompile();
    });
  }, []);

  const handleCompile = useCallback(async () => {
    if (!compilerRef.current || compiling) return;

    setCompiling(true);
    setError(null);
    setStatus('compiling');

    try {
      // Extract plan name from code
      const nameMatch = code.match(/name:\s*['"]([^'"]+)['"]/);
      const planName = nameMatch?.[1] ?? 'live_plan';

      const result = await compilerRef.current.compilePlan(code, planName) as {
        success: boolean;
        artifact?: Record<string, unknown>;
        error?: string;
        phase?: string;
      };

      if (result.success && result.artifact) {
        // Cast through unknown since compiler returns Record<string, unknown>
        loadPlan(result.artifact as unknown as import('../types').PlanJson, `${planName}.plan.ts (live)`);
        setStatus('success');
      } else {
        setError(`${result.phase}: ${result.error}`);
        setStatus('error');
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : String(err));
      setStatus('error');
    } finally {
      setCompiling(false);
    }
  }, [code, compiling, loadPlan]);

  const getStatusText = () => {
    switch (status) {
      case 'compiling': return compilerReady ? 'Compiling...' : 'Loading compiler...';
      case 'success': return 'Compiled successfully';
      case 'error': return 'Compilation failed';
      default: return 'Ready (Cmd+Enter to compile)';
    }
  };

  const getStatusStyle = () => {
    switch (status) {
      case 'error': return { ...styles.status, ...styles.statusError };
      case 'success': return { ...styles.status, ...styles.statusSuccess };
      default: return styles.status;
    }
  };

  return (
    <div style={styles.container}>
      <div style={styles.toolbar}>
        <button
          style={{
            ...styles.button,
            ...(compiling || !compilerReady ? styles.buttonDisabled : {}),
          }}
          onClick={handleCompile}
          disabled={compiling || !compilerReady}
        >
          {compiling ? 'Compiling...' : 'Compile & Visualize'}
        </button>
        <span style={getStatusStyle()}>{getStatusText()}</span>
      </div>

      <div style={styles.editorContainer}>
        <Editor
          height="100%"
          language="typescript"
          value={code}
          onChange={(v) => setCode(v ?? '')}
          theme="vs-dark"
          onMount={handleEditorMount}
          options={{
            minimap: { enabled: false },
            fontSize: 13,
            fontFamily: "'SF Mono', Menlo, Monaco, 'Courier New', monospace",
            tabSize: 2,
            scrollBeyondLastLine: false,
            lineNumbers: 'on',
            renderLineHighlight: 'all',
            bracketPairColorization: { enabled: true },
            guides: {
              bracketPairs: true,
              indentation: true,
            },
          }}
        />
      </div>

      {error && <div style={styles.errorBanner}>{error}</div>}
    </div>
  );
}
