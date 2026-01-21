import { useState, useCallback, useRef } from 'react';
import { useStore } from './state/store';
import DockLayout, { DockLayoutHandle } from './components/DockLayout';
import PlanSelector from './components/PlanSelector';
import MenuBar from './components/MenuBar';

import { dracula } from './theme';

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    height: '100%',
    background: dracula.background,
    color: dracula.foreground,
  },
  header: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '8px 16px',
    background: dracula.headerBg,
    borderBottom: `1px solid ${dracula.border}`,
  },
  headerLeft: {
    display: 'flex',
    alignItems: 'center',
  },
  title: {
    fontSize: '16px',
    fontWeight: 600,
    color: dracula.foreground,
    cursor: 'pointer',
    transition: 'color 0.2s',
  },
  fileName: {
    fontSize: '12px',
    color: dracula.comment,
    marginLeft: '12px',
    fontFamily: 'ui-monospace, monospace',
  },
  main: {
    display: 'flex',
    flex: 1,
    overflow: 'hidden',
  },
  backButton: {
    padding: '4px 12px',
    border: `1px solid ${dracula.border}`,
    borderRadius: '4px',
    fontSize: '12px',
    cursor: 'pointer',
    background: 'transparent',
    color: dracula.comment,
    transition: 'all 0.2s',
  },
};

export default function App() {
  const fileName = useStore((s) => s.fileName);
  const planJson = useStore((s) => s.planJson);
  const clearPlan = useStore((s) => s.clearPlan);

  const [mode, setMode] = useState<'view' | 'edit'>('view');
  const [existingPanels, setExistingPanels] = useState<Set<string>>(new Set());
  const dockLayoutRef = useRef<DockLayoutHandle>(null);

  const handleGoHome = useCallback(() => {
    clearPlan();
    setMode('view');
  }, [clearPlan]);

  const handleCreateNew = useCallback(() => {
    setMode('edit');
  }, []);

  const handleAddPanel = useCallback((panel: 'editor' | 'canvas' | 'details' | 'source') => {
    dockLayoutRef.current?.addPanel(panel);
  }, []);

  const handleResetLayout = useCallback(() => {
    dockLayoutRef.current?.resetLayout();
  }, []);

  const handlePanelsChange = useCallback((panels: Set<string>) => {
    setExistingPanels(panels);
  }, []);

  // Show simple home page when in view mode with no plan
  const showHomePage = mode === 'view' && !planJson;

  return (
    <div style={styles.container}>
      <header style={styles.header}>
        <div style={styles.headerLeft}>
          <span
            style={styles.title}
            onClick={handleGoHome}
            onMouseEnter={(e) => (e.currentTarget.style.color = dracula.purple)}
            onMouseLeave={(e) => (e.currentTarget.style.color = dracula.foreground)}
          >
            Plan Visualizer
          </span>
          {fileName && <span style={styles.fileName}>{fileName}</span>}
          {mode === 'edit' && !fileName && <span style={styles.fileName}>New Plan</span>}
          {!showHomePage && (
            <MenuBar
              onAddPanel={handleAddPanel}
              onResetLayout={handleResetLayout}
              existingPanels={existingPanels}
              mode={mode}
            />
          )}
        </div>
        {(mode === 'edit' || planJson) && (
          <button
            style={styles.backButton}
            onClick={handleGoHome}
            onMouseEnter={(e) => {
              e.currentTarget.style.borderColor = dracula.purple;
              e.currentTarget.style.color = dracula.foreground;
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.borderColor = dracula.border;
              e.currentTarget.style.color = dracula.comment;
            }}
          >
            ← Back to Plans
          </button>
        )}
      </header>
      <main style={styles.main}>
        {showHomePage ? (
          <PlanSelector onCreateNew={handleCreateNew} />
        ) : (
          <DockLayout ref={dockLayoutRef} mode={mode} onPanelsChange={handlePanelsChange} />
        )}
      </main>
    </div>
  );
}
