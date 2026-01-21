import { useStore } from '../state/store';
import type { ExprNode, PredNode } from '../types';
import { dracula } from '../theme';

const styles: Record<string, React.CSSProperties> = {
  panel: {
    width: '280px',
    background: dracula.headerBg,
    borderLeft: `1px solid ${dracula.border}`,
    padding: '16px',
    overflowY: 'auto',
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
          <pre style={styles.code}>{formatExpr(expr)}</pre>
        </div>
      )}

      {pred && (
        <div style={styles.section}>
          <div style={styles.label}>Predicate ({predId})</div>
          <pre style={styles.code}>{formatPred(pred)}</pre>
        </div>
      )}
    </div>
  );
}

function formatExpr(expr: ExprNode): string {
  switch (expr.op) {
    case 'const_number':
      return `${expr.value}`;
    case 'const_null':
      return 'null';
    case 'key_ref':
      return `Key[${expr.key_id}]`;
    case 'param_ref':
      return `Param[${expr.param_id}]`;
    case 'add':
    case 'sub':
    case 'mul':
      return `(${formatExpr(expr.a!)} ${expr.op === 'add' ? '+' : expr.op === 'sub' ? '-' : '*'} ${formatExpr(expr.b!)})`;
    case 'neg':
      return `-${formatExpr(expr.x!)}`;
    case 'coalesce':
      return `coalesce(${formatExpr(expr.a!)}, ${formatExpr(expr.b!)})`;
    default:
      return JSON.stringify(expr);
  }
}

function formatPred(pred: PredNode): string {
  switch (pred.op) {
    case 'const_bool':
      return String(pred.value);
    case 'and':
      return `(${formatPred(pred.a as PredNode)} AND ${formatPred(pred.b as PredNode)})`;
    case 'or':
      return `(${formatPred(pred.a as PredNode)} OR ${formatPred(pred.b as PredNode)})`;
    case 'not':
      return `NOT ${formatPred(pred.x as PredNode)}`;
    case 'cmp':
      return `(${formatExpr(pred.a as ExprNode)} ${pred.cmp} ${formatExpr(pred.b as ExprNode)})`;
    case 'in':
      return `${formatExpr(pred.lhs!)} IN [${pred.list!.join(', ')}]`;
    case 'is_null':
      return `IS_NULL(${formatExpr(pred.x as ExprNode)})`;
    case 'not_null':
      return `NOT_NULL(${formatExpr(pred.x as ExprNode)})`;
    case 'regex':
      const pattern =
        pred.pattern?.kind === 'literal'
          ? `"${pred.pattern.value}"`
          : `Param[${pred.pattern?.param_id}]`;
      return `Key[${pred.key_id}] REGEX ${pattern}`;
    default:
      return JSON.stringify(pred);
  }
}
