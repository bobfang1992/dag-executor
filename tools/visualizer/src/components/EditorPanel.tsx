import { useState, useCallback, useRef, useEffect } from 'react';
import Editor, { OnMount } from '@monaco-editor/react';
import type { editor } from 'monaco-editor';
import { useStore } from '../state/store';
import { dracula } from '../theme';
import { PromptModal, ConfirmModal } from './Modal';
import Dropdown from './Dropdown';
import * as prefs from '../state/preferences';
import type { SavedPlan, SavedPlansState } from '../state/preferences';

// Import generated DSL types for Monaco intellisense
import { DSL_TYPES } from '@ranking-dsl/generated';

// Detect Mac for keyboard shortcut display
const isMac = typeof navigator !== 'undefined' && /Mac|iPod|iPhone|iPad/.test(navigator.platform);
const modKey = isMac ? '⌘' : 'Ctrl+';

function generateId(): string {
  return Math.random().toString(36).substring(2, 10);
}

// Compress/decompress for URL sharing using URL-safe base64
function encodeForUrl(code: string): string {
  try {
    // Use TextEncoder for proper UTF-8 handling, then base64
    const bytes = new TextEncoder().encode(code);
    // Build binary string in chunks to avoid stack overflow on large plans
    // (String.fromCharCode(...bytes) throws for arrays > ~60k elements)
    let binary = '';
    const chunkSize = 8192;
    for (let i = 0; i < bytes.length; i += chunkSize) {
      const chunk = bytes.subarray(i, i + chunkSize);
      binary += String.fromCharCode.apply(null, Array.from(chunk));
    }
    // Use encodeURIComponent to make base64 URL-safe (handles +, /, = characters)
    return encodeURIComponent(btoa(binary));
  } catch {
    return '';
  }
}

function decodeFromUrl(encoded: string): string | null {
  try {
    // Decode URL encoding first, then base64
    const base64 = decodeURIComponent(encoded);
    const binary = atob(base64);
    const bytes = Uint8Array.from(binary, c => c.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  } catch {
    return null;
  }
}

const DEFAULT_PLAN = `import { definePlan, EP } from '@ranking-dsl/runtime';
// Key, P, coalesce, regex are globals - no import needed

export default definePlan({
  name: 'my_plan',
  build: (ctx) => {
    // Source: viewer returns the request user's data, follow fans out to followees
    const source = ctx.viewer({ endpoint: EP.redis.default }).follow({ endpoint: EP.redis.default, fanout: 100 });

    // Score: compute final_score using natural expression syntax
    // The compiler extracts Key.x * P.y expressions at compile-time
    const scored = source.vm({
      outKey: Key.final_score,
      expr: Key.model_score_1 + Key.model_score_2 * coalesce(P.media_age_penalty_weight, 0.2),
    });

    // Filter: natural predicate syntax (extracted at compile-time)
    // Supports: &&, ||, !, comparisons (==, !=, <, <=, >, >=), null checks, regex()
    const filtered = scored.filter({
      pred: Key.final_score >= 0.5 && Key.country != null,
    });

    // Alternative: regex pattern matching
    // const filtered = scored.filter({
    //   pred: regex(Key.title, "^trending"),
    // });

    // Return top 10
    return filtered.take({ count: 10 });
  },
});
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
  secondaryButton: {
    background: 'transparent',
    color: dracula.comment,
    border: `1px solid ${dracula.border}`,
    padding: '5px 12px',
    borderRadius: '4px',
    cursor: 'pointer',
    fontSize: '12px',
    marginLeft: '8px',
    transition: 'all 0.2s',
  },
  buttonGroup: {
    display: 'flex',
    alignItems: 'center',
  },
  shareToast: {
    position: 'fixed' as const,
    bottom: '20px',
    left: '50%',
    transform: 'translateX(-50%)',
    background: dracula.green,
    color: dracula.background,
    padding: '8px 16px',
    borderRadius: '4px',
    fontSize: '13px',
    fontWeight: 500,
    zIndex: 1000,
  },
  planSelector: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    padding: '6px 12px',
    background: dracula.currentLine,
    borderBottom: `1px solid ${dracula.border}`,
  },
  smallButton: {
    background: 'transparent',
    color: dracula.comment,
    border: `1px solid ${dracula.border}`,
    padding: '3px 8px',
    borderRadius: '4px',
    cursor: 'pointer',
    fontSize: '11px',
    transition: 'all 0.2s',
  },
  deleteButton: {
    background: 'transparent',
    color: dracula.red,
    border: `1px solid ${dracula.red}44`,
    padding: '3px 8px',
    borderRadius: '4px',
    cursor: 'pointer',
    fontSize: '11px',
    transition: 'all 0.2s',
  },
  planLabel: {
    fontSize: '11px',
    color: dracula.comment,
  },
};

// Get initial code: URL hash > localStorage > default
function getInitialCode(): string {
  // Check URL hash first (for shared links)
  if (typeof window !== 'undefined' && window.location.hash) {
    const hash = window.location.hash.slice(1);
    const params = new URLSearchParams(hash);
    const encoded = params.get('code');
    if (encoded) {
      const decoded = decodeFromUrl(encoded);
      if (decoded) {
        // Clear hash after loading to avoid re-loading on refresh
        window.history.replaceState(null, '', window.location.pathname + window.location.search);
        return decoded;
      }
    }
  }

  // Check localStorage via preferences module
  const saved = prefs.getEditorCode();
  if (saved) return saved;

  return DEFAULT_PLAN;
}

export default function EditorPanel() {
  const [code, setCode] = useState(getInitialCode);
  const [compiling, setCompiling] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState<'ready' | 'compiling' | 'success' | 'error'>('ready');
  const [showToast, setShowToast] = useState(false);
  const [toastMessage, setToastMessage] = useState('');
  const [savedPlans, setSavedPlans] = useState<SavedPlansState>(prefs.getSavedPlans);
  const [currentPlanId, setCurrentPlanId] = useState<string | null>(savedPlans.currentId);
  const [modal, setModal] = useState<
    | { type: 'saveAs'; defaultName: string }
    | { type: 'rename'; currentName: string }
    | { type: 'delete'; planName: string }
    | null
  >(null);

  const editorRef = useRef<editor.IStandaloneCodeEditor | null>(null);

  // Refs for handlers to avoid stale closures in Monaco keyboard shortcuts
  const handleCompileRef = useRef<() => void>(() => {});
  const handleSaveAsRef = useRef<() => void>(() => {});
  const handleFormatRef = useRef<() => void>(() => {});

  const loadPlan = useStore((s) => s.loadPlan);
  const editSourceCode = useStore((s) => s.editSourceCode);
  const setEditSourceCode = useStore((s) => s.setEditSourceCode);

  // Load source code from store if set (when editing existing plan)
  useEffect(() => {
    if (editSourceCode) {
      setCode(editSourceCode);
      setCurrentPlanId(null); // Not a saved plan, it's from the viewed plan
      // Clear editSourceCode so it doesn't keep overwriting
      setEditSourceCode(null);
    }
  }, [editSourceCode, setEditSourceCode]);

  // Save to localStorage on code change (debounced)
  useEffect(() => {
    const timer = setTimeout(() => {
      prefs.setEditorCode(code);
      // Also update current plan if one is selected
      if (currentPlanId) {
        setSavedPlans(prev => {
          const updated = {
            ...prev,
            plans: prev.plans.map(p =>
              p.id === currentPlanId ? { ...p, code, updatedAt: Date.now() } : p
            ),
          };
          prefs.setSavedPlans(updated);
          return updated;
        });
      }
    }, 500);
    return () => clearTimeout(timer);
  }, [code, currentPlanId]);

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

    // Suppress TypeScript errors for natural expression/predicate syntax
    // These are extracted by the compiler at compile-time via AST transformation
    // Error codes:
    // - 2304: "Cannot find name 'X'" (Key, P, coalesce, regex are globals injected by compiler)
    // - 2362: "The left-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type"
    // - 2363: "The right-hand side of an arithmetic operation must be of type 'any', 'number', 'bigint' or an enum type"
    // - 2322: "Type 'X' is not assignable to type 'Y'" (result type mismatch)
    // - 2365: "Operator 'X' cannot be applied to types 'Y' and 'Z'" (comparison operators)
    // - 2367: "This comparison appears to be unintentional" (null comparisons)
    monaco.languages.typescript.typescriptDefaults.setDiagnosticsOptions({
      diagnosticCodesToIgnore: [2304, 2362, 2363, 2322, 2365, 2367],
    });

    // Add DSL type definitions
    monaco.languages.typescript.typescriptDefaults.addExtraLib(
      DSL_TYPES,
      'file:///node_modules/@ranking-dsl/index.d.ts'
    );

    // Add keyboard shortcut for compile (Cmd/Ctrl+Enter)
    // Use refs to avoid stale closures
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => {
      handleCompileRef.current();
    });

    // Add keyboard shortcut for save (Cmd/Ctrl+S)
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => {
      handleSaveAsRef.current();
    });

    // Add keyboard shortcut for format (Cmd/Ctrl+Shift+F)
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.KeyF, () => {
      handleFormatRef.current();
    });
  }, []);

  const handleCompile = useCallback(async () => {
    if (compiling) return;

    setCompiling(true);
    setError(null);
    setStatus('compiling');

    try {
      // Extract plan name from code
      const nameMatch = code.match(/name:\s*['"]([^'"]+)['"]/);
      const planName = nameMatch?.[1] ?? 'live_plan';
      const filename = `${planName}.plan.ts`;

      // Call server-side compiler API (uses real dslc for full parity)
      const response = await fetch('/api/compile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ source: code, filename }),
      });

      const result = await response.json() as {
        success: boolean;
        artifact?: Record<string, unknown>;
        error?: string;
        phase?: string;
      };

      if (result.success && result.artifact) {
        // Cast through unknown since compiler returns Record<string, unknown>
        // Use skipHistory to avoid pushing ?plan= for live editor plans
        // (they're not in the index, so refresh would fail to find them)
        loadPlan(
          result.artifact as unknown as import('../types').PlanJson,
          `${planName}.plan.ts (live)`,
          { skipHistory: true }
        );
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

  const showToastWithMessage = useCallback((message: string) => {
    setToastMessage(message);
    setShowToast(true);
    setTimeout(() => setShowToast(false), 2000);
  }, []);

  const handleShare = useCallback(() => {
    const encoded = encodeForUrl(code);
    if (!encoded) return;

    const url = `${window.location.origin}${window.location.pathname}#code=${encoded}`;
    navigator.clipboard.writeText(url).then(() => {
      showToastWithMessage('Link copied to clipboard!');
    });
  }, [code, showToastWithMessage]);

  const handleReset = useCallback(() => {
    setCode(DEFAULT_PLAN);
    setCurrentPlanId(null);
    prefs.removeEditorCode();
    const updated = { ...savedPlans, currentId: null };
    setSavedPlans(updated);
    prefs.setSavedPlans(updated);
  }, [savedPlans]);

  const handleFormat = useCallback(() => {
    if (editorRef.current) {
      editorRef.current.getAction('editor.action.formatDocument')?.run();
    }
  }, []);

  const handleSaveAs = useCallback(() => {
    const nameMatch = code.match(/name:\s*['"]([^'"]+)['"]/);
    const defaultName = nameMatch?.[1] ?? 'my_plan';
    setModal({ type: 'saveAs', defaultName });
  }, [code]);

  // Keep handler refs updated to avoid stale closures in Monaco shortcuts
  useEffect(() => {
    handleCompileRef.current = handleCompile;
  }, [handleCompile]);

  useEffect(() => {
    handleSaveAsRef.current = handleSaveAs;
  }, [handleSaveAs]);

  useEffect(() => {
    handleFormatRef.current = handleFormat;
  }, [handleFormat]);

  const confirmSaveAs = useCallback((name: string) => {
    const newPlan: SavedPlan = {
      id: generateId(),
      name,
      code,
      updatedAt: Date.now(),
    };

    const updated: SavedPlansState = {
      plans: [...savedPlans.plans, newPlan],
      currentId: newPlan.id,
    };
    setSavedPlans(updated);
    setCurrentPlanId(newPlan.id);
    prefs.setSavedPlans(updated);
    setModal(null);
    showToastWithMessage(`Saved "${name}"`);
  }, [code, savedPlans, showToastWithMessage]);

  const handleSelectPlan = useCallback((id: string) => {
    if (id === '__new__') {
      setCode(DEFAULT_PLAN);
      setCurrentPlanId(null);
      const updated = { ...savedPlans, currentId: null };
      setSavedPlans(updated);
      prefs.setSavedPlans(updated);
      return;
    }

    const plan = savedPlans.plans.find(p => p.id === id);
    if (plan) {
      setCode(plan.code);
      setCurrentPlanId(plan.id);
      const updated = { ...savedPlans, currentId: plan.id };
      setSavedPlans(updated);
      prefs.setSavedPlans(updated);
    }
  }, [savedPlans]);

  const handleDeletePlan = useCallback(() => {
    if (!currentPlanId) return;
    const plan = savedPlans.plans.find(p => p.id === currentPlanId);
    if (!plan) return;
    setModal({ type: 'delete', planName: plan.name });
  }, [currentPlanId, savedPlans]);

  const confirmDelete = useCallback(() => {
    if (!currentPlanId) return;
    const plan = savedPlans.plans.find(p => p.id === currentPlanId);

    const updated: SavedPlansState = {
      plans: savedPlans.plans.filter(p => p.id !== currentPlanId),
      currentId: null,
    };
    setSavedPlans(updated);
    setCurrentPlanId(null);
    prefs.setSavedPlans(updated);
    setModal(null);
    if (plan) showToastWithMessage(`Deleted "${plan.name}"`);
  }, [currentPlanId, savedPlans, showToastWithMessage]);

  const handleRenamePlan = useCallback(() => {
    if (!currentPlanId) return;
    const plan = savedPlans.plans.find(p => p.id === currentPlanId);
    if (!plan) return;
    setModal({ type: 'rename', currentName: plan.name });
  }, [currentPlanId, savedPlans]);

  const confirmRename = useCallback((newName: string) => {
    if (!currentPlanId) return;

    const updated: SavedPlansState = {
      ...savedPlans,
      plans: savedPlans.plans.map(p =>
        p.id === currentPlanId ? { ...p, name: newName } : p
      ),
    };
    setSavedPlans(updated);
    prefs.setSavedPlans(updated);
    setModal(null);
    showToastWithMessage(`Renamed to "${newName}"`);
  }, [currentPlanId, savedPlans, showToastWithMessage]);

  const getStatusText = () => {
    switch (status) {
      case 'compiling': return 'Compiling...';
      case 'success': return 'Compiled successfully';
      case 'error': return 'Compilation failed';
      default: return `Ready (${modKey}Enter to compile)`;
    }
  };

  const getStatusStyle = () => {
    switch (status) {
      case 'error': return { ...styles.status, ...styles.statusError };
      case 'success': return { ...styles.status, ...styles.statusSuccess };
      default: return styles.status;
    }
  };

  const currentPlan = savedPlans.plans.find(p => p.id === currentPlanId);

  return (
    <div style={styles.container}>
      {/* Saved Plans Selector */}
      <div style={styles.planSelector}>
        <span style={styles.planLabel}>Plan:</span>
        <Dropdown
          value={currentPlanId ?? '__new__'}
          onChange={handleSelectPlan}
          options={[
            { value: '__new__', label: 'New Plan' },
            ...savedPlans.plans.map(plan => ({ value: plan.id, label: plan.name })),
          ]}
        />
        <button
          style={styles.smallButton}
          onClick={handleSaveAs}
          title={`Save plan to library (${modKey}S)`}
          onMouseEnter={(e) => {
            e.currentTarget.style.borderColor = dracula.purple;
            e.currentTarget.style.color = dracula.foreground;
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.borderColor = dracula.border;
            e.currentTarget.style.color = dracula.comment;
          }}
        >
          Save As
        </button>
        {currentPlanId && (
          <>
            <button
              style={styles.smallButton}
              onClick={handleRenamePlan}
              onMouseEnter={(e) => {
                e.currentTarget.style.borderColor = dracula.purple;
                e.currentTarget.style.color = dracula.foreground;
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.borderColor = dracula.border;
                e.currentTarget.style.color = dracula.comment;
              }}
            >
              Rename
            </button>
            <button
              style={styles.deleteButton}
              onClick={handleDeletePlan}
              onMouseEnter={(e) => {
                e.currentTarget.style.background = `${dracula.red}22`;
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'transparent';
              }}
            >
              Delete
            </button>
          </>
        )}
        {currentPlan && (
          <span style={{ ...styles.planLabel, marginLeft: 'auto' }}>
            Last saved: {new Date(currentPlan.updatedAt).toLocaleTimeString()}
          </span>
        )}
      </div>

      <div style={styles.toolbar}>
        <div style={styles.buttonGroup}>
          <button
            style={{
              ...styles.button,
              ...(compiling ? styles.buttonDisabled : {}),
            }}
            onClick={handleCompile}
            disabled={compiling}
            title={`Compile and visualize the plan (${modKey}Enter)`}
          >
            {compiling ? 'Compiling...' : 'Compile & Visualize'}
          </button>
          <button
            style={styles.secondaryButton}
            onClick={handleShare}
            title="Copy shareable link to clipboard"
            onMouseEnter={(e) => {
              e.currentTarget.style.borderColor = dracula.purple;
              e.currentTarget.style.color = dracula.foreground;
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.borderColor = dracula.border;
              e.currentTarget.style.color = dracula.comment;
            }}
          >
            Share
          </button>
          <button
            style={styles.secondaryButton}
            onClick={handleFormat}
            title={`Format code (${modKey}Shift+F)`}
            onMouseEnter={(e) => {
              e.currentTarget.style.borderColor = dracula.purple;
              e.currentTarget.style.color = dracula.foreground;
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.borderColor = dracula.border;
              e.currentTarget.style.color = dracula.comment;
            }}
          >
            Format
          </button>
          <button
            style={styles.secondaryButton}
            onClick={handleReset}
            title="Reset to default plan template"
            onMouseEnter={(e) => {
              e.currentTarget.style.borderColor = dracula.purple;
              e.currentTarget.style.color = dracula.foreground;
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.borderColor = dracula.border;
              e.currentTarget.style.color = dracula.comment;
            }}
          >
            Reset
          </button>
        </div>
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
      {showToast && <div style={styles.shareToast}>{toastMessage}</div>}

      {/* Modals */}
      {modal?.type === 'saveAs' && (
        <PromptModal
          title="Save Plan"
          message="Enter a name for this plan:"
          defaultValue={modal.defaultName}
          placeholder="Plan name"
          onConfirm={confirmSaveAs}
          onCancel={() => setModal(null)}
        />
      )}
      {modal?.type === 'rename' && (
        <PromptModal
          title="Rename Plan"
          message="Enter a new name:"
          defaultValue={modal.currentName}
          placeholder="Plan name"
          onConfirm={confirmRename}
          onCancel={() => setModal(null)}
        />
      )}
      {modal?.type === 'delete' && (
        <ConfirmModal
          title="Delete Plan"
          message={`Are you sure you want to delete "${modal.planName}"? This cannot be undone.`}
          confirmLabel="Delete"
          danger
          onConfirm={confirmDelete}
          onCancel={() => setModal(null)}
        />
      )}
    </div>
  );
}
