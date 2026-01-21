import { useStore } from '../state/store';
import { dracula } from '../theme';

interface ToolbarProps {
  onEdit?: () => void;
}

const styles: Record<string, React.CSSProperties> = {
  dock: {
    position: 'absolute',
    bottom: '16px',
    left: '50%',
    transform: 'translateX(-50%)',
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    padding: '8px 16px',
    background: 'rgba(255, 255, 255, 0.9)',
    backdropFilter: 'blur(10px)',
    borderRadius: '12px',
    border: `1px solid ${dracula.border}`,
    boxShadow: '0 4px 20px rgba(0, 0, 0, 0.1)',
    zIndex: 100,
  },
  button: {
    padding: '8px 14px',
    background: dracula.buttonBg,
    border: `1px solid ${dracula.border}`,
    borderRadius: '6px',
    color: dracula.foreground,
    fontSize: '12px',
    cursor: 'pointer',
    transition: 'all 0.2s',
  },
  badge: {
    padding: '6px 10px',
    background: dracula.background,
    border: `1px solid ${dracula.border}`,
    borderRadius: '6px',
    fontSize: '11px',
    color: dracula.comment,
  },
};

export default function Toolbar({ onEdit }: ToolbarProps) {
  const graph = useStore((s) => s.graph);
  const planJson = useStore((s) => s.planJson);
  const sourceCode = useStore((s) => s.sourceCode);
  const fitToView = useStore((s) => s.fitToView);

  if (!planJson) return null;

  const nodeCount = graph?.nodes.size ?? 0;
  const edgeCount = graph?.edges.length ?? 0;

  const handleFit = () => {
    // Get the canvas container (parent of canvas element), not the canvas itself
    // PixiJS canvas has different dimensions due to resolution/autoDensity
    const canvas = document.querySelector('canvas');
    const container = canvas?.parentElement;
    if (container) {
      fitToView(container.clientWidth, container.clientHeight);
    }
  };

  return (
    <div style={styles.dock}>
      <span style={styles.badge}>
        {nodeCount} nodes, {edgeCount} edges
      </span>
      <button
        style={styles.button}
        onClick={handleFit}
        onMouseOver={(e) => (e.currentTarget.style.background = dracula.buttonHover)}
        onMouseOut={(e) => (e.currentTarget.style.background = dracula.buttonBg)}
      >
        Fit
      </button>
      {sourceCode && onEdit && (
        <button
          style={{ ...styles.button, background: dracula.green, color: dracula.background }}
          onClick={onEdit}
          onMouseOver={(e) => (e.currentTarget.style.opacity = '0.8')}
          onMouseOut={(e) => (e.currentTarget.style.opacity = '1')}
        >
          Edit
        </button>
      )}
    </div>
  );
}
