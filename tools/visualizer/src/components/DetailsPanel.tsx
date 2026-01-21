import { useStore } from '../state/store';
import type { ExprNode, PredNode } from '../types';
import { dracula } from '../theme';

// Navigate to registry entry
function navigateToRegistry(tab: 'keys' | 'params', id: number) {
  const url = new URL(window.location.href);
  url.searchParams.set('view', 'registries');
  url.searchParams.set('tab', tab);
  url.searchParams.set('selected', String(id));
  window.location.href = url.toString();
}

const styles: Record<string, React.CSSProperties> = {
  panel: {
    width: '100%',
    height: '100%',
    background: dracula.headerBg,
    borderLeft: `1px solid ${dracula.border}`,
    padding: '16px',
    overflowY: 'auto',
    boxSizing: 'border-box',
  },
  collapsedPanel: {
    width: '36px',
    background: dracula.headerBg,
    borderLeft: `1px solid ${dracula.border}`,
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    paddingTop: '8px',
  },
  toggleButton: {
    width: '28px',
    height: '28px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    background: 'transparent',
    border: `1px solid ${dracula.border}`,
    borderRadius: '4px',
    cursor: 'pointer',
    color: dracula.comment,
    fontSize: '14px',
    transition: 'all 0.2s',
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: '12px',
  },
  title: {
    fontSize: '14px',
    fontWeight: 600,
    color: dracula.foreground,
    fontFamily: 'ui-monospace, monospace',
  },
  section: {
    marginBottom: '16px',
  },
  label: {
    fontSize: '11px',
    color: dracula.comment,
    marginBottom: '4px',
    textTransform: 'uppercase',
  },
  value: {
    fontSize: '13px',
    color: dracula.foreground,
    wordBreak: 'break-all',
    fontFamily: 'ui-monospace, monospace',
  },
  code: {
    fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
    fontSize: '11px',
    background: dracula.background,
    padding: '8px',
    borderRadius: '4px',
    whiteSpace: 'pre-wrap',
    wordBreak: 'break-all',
    color: dracula.green,
    border: `1px solid ${dracula.border}`,
  },
  badge: {
    display: 'inline-block',
    padding: '2px 6px',
    background: dracula.currentLine,
    color: dracula.foreground,
    borderRadius: '3px',
    fontSize: '11px',
    marginRight: '4px',
    fontWeight: 600,
    border: `1px solid ${dracula.border}`,
  },
  link: {
    color: dracula.purple,
    cursor: 'pointer',
    textDecoration: 'none',
    borderBottom: `1px dotted ${dracula.purple}`,
  },
};

export default function DetailsPanel() {
  const selectedNodeId = useStore((s) => s.selectedNodeId);
  const graph = useStore((s) => s.graph);
  const planJson = useStore((s) => s.planJson);
  const collapsed = useStore((s) => s.detailsCollapsed);
  const toggleCollapsed = useStore((s) => s.toggleDetailsCollapsed);

  if (collapsed) {
    return (
      <div style={styles.collapsedPanel}>
        <button
          style={styles.toggleButton}
          onClick={toggleCollapsed}
          onMouseEnter={(e) => {
            e.currentTarget.style.borderColor = dracula.purple;
            e.currentTarget.style.color = dracula.foreground;
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.borderColor = dracula.border;
            e.currentTarget.style.color = dracula.comment;
          }}
          title="Show details panel"
        >
          ‹
        </button>
      </div>
    );
  }

  if (!selectedNodeId || !graph || !planJson) {
    return (
      <div style={styles.panel}>
        <div style={styles.header}>
          <div style={styles.title}>Details</div>
          <button
            style={styles.toggleButton}
            onClick={toggleCollapsed}
            onMouseEnter={(e) => {
              e.currentTarget.style.borderColor = dracula.purple;
              e.currentTarget.style.color = dracula.foreground;
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.borderColor = dracula.border;
              e.currentTarget.style.color = dracula.comment;
            }}
            title="Hide details panel"
          >
            ›
          </button>
        </div>
        <div style={{ ...styles.value, color: dracula.comment }}>
          Click a node to see details
        </div>
      </div>
    );
  }

  const node = graph.nodes.get(selectedNodeId);
  if (!node) return null;

  const incomingEdges = graph.edges.filter((e) => e.to === selectedNodeId);
  const outgoingEdges = graph.edges.filter((e) => e.from === selectedNodeId);

  // Get expression/predicate if applicable
  const exprId = node.params.expr_id as string | undefined;
  const predId = node.params.pred_id as string | undefined;
  const expr = exprId ? planJson.expr_table?.[exprId] : undefined;
  const pred = predId ? planJson.pred_table?.[predId] : undefined;

  return (
    <div style={styles.panel}>
      <div style={styles.header}>
        <div style={styles.title}>{node.label}</div>
        <button
          style={styles.toggleButton}
          onClick={toggleCollapsed}
          onMouseEnter={(e) => {
            e.currentTarget.style.borderColor = dracula.purple;
            e.currentTarget.style.color = dracula.foreground;
          }}
          onMouseLeave={(e) => {
            e.currentTarget.style.borderColor = dracula.border;
            e.currentTarget.style.color = dracula.comment;
          }}
          title="Hide details panel"
        >
          ›
        </button>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Node ID</div>
        <div style={styles.value}>{node.id}</div>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Operation</div>
        <div style={styles.value}>{node.op}</div>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Flags</div>
        <div>
          {node.isSource && <span style={styles.badge}>Source</span>}
          {node.isOutput && (
            <span style={{ ...styles.badge, background: dracula.red }}>Output</span>
          )}
        </div>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Inputs ({incomingEdges.length})</div>
        <div style={styles.value}>
          {incomingEdges.length > 0
            ? incomingEdges.map((e) => e.from).join(', ')
            : 'None'}
        </div>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Outputs ({outgoingEdges.length})</div>
        <div style={styles.value}>
          {outgoingEdges.length > 0
            ? outgoingEdges.map((e) => e.to).join(', ')
            : 'None'}
        </div>
      </div>

      <div style={styles.section}>
        <div style={styles.label}>Params</div>
        <pre style={styles.code}>
          {JSON.stringify(node.params, null, 2)}
        </pre>
      </div>

      {expr && (
        <div style={styles.section}>
          <div style={styles.label}>Expression ({exprId})</div>
          <pre style={styles.code}>{formatExprJsx(expr)}</pre>
        </div>
      )}

      {pred && (
        <div style={styles.section}>
          <div style={styles.label}>Predicate ({predId})</div>
          <pre style={styles.code}>{formatPredJsx(pred)}</pre>
        </div>
      )}
    </div>
  );
}

// Clickable key reference
function KeyRef({ keyId }: { keyId: number }) {
  return (
    <span
      style={styles.link}
      onClick={(e) => {
        e.stopPropagation();
        navigateToRegistry('keys', keyId);
      }}
      title={`View Key ${keyId} in registry`}
    >
      Key[{keyId}]
    </span>
  );
}

// Clickable param reference
function ParamRef({ paramId }: { paramId: number }) {
  return (
    <span
      style={styles.link}
      onClick={(e) => {
        e.stopPropagation();
        navigateToRegistry('params', paramId);
      }}
      title={`View Param ${paramId} in registry`}
    >
      Param[{paramId}]
    </span>
  );
}

function formatExprJsx(expr: ExprNode, key: number = 0): React.ReactNode {
  switch (expr.op) {
    case 'const_number':
      return <span key={key}>{expr.value}</span>;
    case 'const_null':
      return <span key={key}>null</span>;
    case 'key_ref':
      return <KeyRef key={key} keyId={expr.key_id!} />;
    case 'param_ref':
      return <ParamRef key={key} paramId={expr.param_id!} />;
    case 'add':
    case 'sub':
    case 'mul':
      return (
        <span key={key}>
          ({formatExprJsx(expr.a!, key * 10 + 1)} {expr.op === 'add' ? '+' : expr.op === 'sub' ? '-' : '*'} {formatExprJsx(expr.b!, key * 10 + 2)})
        </span>
      );
    case 'neg':
      return <span key={key}>-{formatExprJsx(expr.x!, key * 10 + 1)}</span>;
    case 'coalesce':
      return (
        <span key={key}>
          coalesce({formatExprJsx(expr.a!, key * 10 + 1)}, {formatExprJsx(expr.b!, key * 10 + 2)})
        </span>
      );
    default:
      return <span key={key}>{JSON.stringify(expr)}</span>;
  }
}

function formatPredJsx(pred: PredNode, key: number = 0): React.ReactNode {
  switch (pred.op) {
    case 'const_bool':
      return <span key={key}>{String(pred.value)}</span>;
    case 'and':
      return (
        <span key={key}>
          ({formatPredJsx(pred.a as PredNode, key * 10 + 1)} AND {formatPredJsx(pred.b as PredNode, key * 10 + 2)})
        </span>
      );
    case 'or':
      return (
        <span key={key}>
          ({formatPredJsx(pred.a as PredNode, key * 10 + 1)} OR {formatPredJsx(pred.b as PredNode, key * 10 + 2)})
        </span>
      );
    case 'not':
      return <span key={key}>NOT {formatPredJsx(pred.x as PredNode, key * 10 + 1)}</span>;
    case 'cmp':
      return (
        <span key={key}>
          ({formatExprJsx(pred.a as ExprNode, key * 10 + 1)} {pred.cmp} {formatExprJsx(pred.b as ExprNode, key * 10 + 2)})
        </span>
      );
    case 'in':
      return (
        <span key={key}>
          {formatExprJsx(pred.lhs!, key * 10 + 1)} IN [{pred.list!.join(', ')}]
        </span>
      );
    case 'is_null':
      return (
        <span key={key}>
          IS_NULL({formatExprJsx(pred.x as ExprNode, key * 10 + 1)})
        </span>
      );
    case 'not_null':
      return (
        <span key={key}>
          NOT_NULL({formatExprJsx(pred.x as ExprNode, key * 10 + 1)})
        </span>
      );
    case 'regex': {
      const patternNode =
        pred.pattern?.kind === 'literal' ? (
          <span>"{pred.pattern.value}"</span>
        ) : (
          <ParamRef paramId={pred.pattern?.param_id!} />
        );
      return (
        <span key={key}>
          <KeyRef keyId={pred.key_id!} /> REGEX {patternNode}
        </span>
      );
    }
    default:
      return <span key={key}>{JSON.stringify(pred)}</span>;
  }
}

