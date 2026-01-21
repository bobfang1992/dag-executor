import { useStore } from '../state/store';
import { dracula } from '../theme';

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

export default function Toolbar() {
  const graph = useStore((s) => s.graph);
  const planJson = useStore((s) => s.planJson);
  const fitToView = useStore((s) => s.fitToView);

  if (!planJson) return null;

  const nodeCount = graph?.nodes.size ?? 0;
  const edgeCount = graph?.edges.length ?? 0;

  const handleFit = () => {
    const canvas = document.querySelector('canvas');
    if (canvas) {
      fitToView(canvas.clientWidth, canvas.clientHeight);
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
    </div>
  );
}
