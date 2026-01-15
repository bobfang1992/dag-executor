import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { oneLight } from 'react-syntax-highlighter/dist/esm/styles/prism';
import { useStore } from '../state/store';
import { dracula } from '../theme';

const styles: Record<string, React.CSSProperties> = {
  panel: {
    width: '100%',
    height: '100%',
    background: dracula.background,
    borderRight: `1px solid ${dracula.border}`,
    display: 'flex',
    flexDirection: 'column',
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '8px 12px',
    background: dracula.currentLine,
    borderBottom: `1px solid ${dracula.border}`,
  },
  title: {
    fontSize: '12px',
    fontWeight: 600,
    color: dracula.foreground,
    fontFamily: 'ui-monospace, monospace',
  },
  closeButton: {
    background: 'none',
    border: 'none',
    color: dracula.comment,
    fontSize: '16px',
    cursor: 'pointer',
    padding: '2px 6px',
    borderRadius: '4px',
  },
  content: {
    flex: 1,
    overflow: 'auto',
    background: dracula.background,
  },
  loading: {
    padding: '12px',
    color: dracula.comment,
    fontStyle: 'italic',
  },
  noSource: {
    padding: '12px',
    color: dracula.comment,
    fontStyle: 'italic',
  },
};

// Custom style overrides for the syntax highlighter
const customStyle: React.CSSProperties = {
  margin: 0,
  padding: '12px',
  background: dracula.background,
  fontSize: '12px',
  lineHeight: 1.6,
  fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, monospace',
};

export default function SourcePanel() {
  const sourceCode = useStore((s) => s.sourceCode);
  const sourceLoading = useStore((s) => s.sourceLoading);
  const planJson = useStore((s) => s.planJson);
  const toggleSource = useStore((s) => s.toggleSource);

  const planName = planJson?.plan_name || 'unknown';

  return (
    <div style={styles.panel}>
      <div style={styles.header}>
        <span style={styles.title}>{planName}.plan.ts</span>
        <button
          style={styles.closeButton}
          onClick={toggleSource}
          onMouseOver={(e) => (e.currentTarget.style.background = dracula.buttonBg)}
          onMouseOut={(e) => (e.currentTarget.style.background = 'none')}
        >
          ✕
        </button>
      </div>
      <div style={styles.content}>
        {sourceLoading ? (
          <div style={styles.loading}>Loading source...</div>
        ) : sourceCode ? (
          <SyntaxHighlighter
            language="typescript"
            style={oneLight}
            customStyle={customStyle}
            showLineNumbers
            lineNumberStyle={{
              minWidth: '40px',
              paddingRight: '12px',
              marginRight: '12px',
              borderRight: `1px solid ${dracula.border}`,
              color: dracula.comment,
              userSelect: 'none',
            }}
          >
            {sourceCode}
          </SyntaxHighlighter>
        ) : (
          <div style={styles.noSource}>
            Source file not found.
            <br />
            <br />
            Expected: plans/{planName}.plan.ts
            <br />
            or: examples/plans/{planName}.plan.ts
          </div>
        )}
      </div>
    </div>
  );
}
