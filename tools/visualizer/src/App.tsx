import { useState, useCallback, useRef, useEffect } from 'react';
import { useStore } from './state/store';
import PlanSelector from './components/PlanSelector';
import Canvas from './components/Canvas';
import DetailsPanel from './components/DetailsPanel';
import Toolbar from './components/Toolbar';
import SourcePanel from './components/SourcePanel';
import EditorPanel from './components/EditorPanel';

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
  canvasContainer: {
    flex: 1,
    position: 'relative',
    minWidth: 0,
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
  const planJson = useStore((s) => s.planJson);
  const fileName = useStore((s) => s.fileName);
  const showSource = useStore((s) => s.showSource);
  const clearPlan = useStore((s) => s.clearPlan);

  const [mode, setMode] = useState<'view' | 'edit'>('view');
  const [sourceWidth, setSourceWidth] = useState(50); // percentage
  const [editorWidth, setEditorWidth] = useState(50); // percentage for edit mode
  const isDraggingRef = useRef<'source' | 'editor' | false>(false);
  const containerRef = useRef<HTMLDivElement>(null);
  const editContainerRef = useRef<HTMLDivElement>(null);

  const handleSourceDividerMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    isDraggingRef.current = 'source';
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, []);

  const handleEditorDividerMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    isDraggingRef.current = 'editor';
    document.body.style.cursor = 'col-resize';
    document.body.style.userSelect = 'none';
  }, []);

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (!isDraggingRef.current) return;

      if (isDraggingRef.current === 'source' && containerRef.current) {
        const rect = containerRef.current.getBoundingClientRect();
        const newWidth = ((e.clientX - rect.left) / rect.width) * 100;
        setSourceWidth(Math.max(20, Math.min(80, newWidth)));
      } else if (isDraggingRef.current === 'editor' && editContainerRef.current) {
        const rect = editContainerRef.current.getBoundingClientRect();
        const newWidth = ((e.clientX - rect.left) / rect.width) * 100;
        setEditorWidth(Math.max(25, Math.min(75, newWidth)));
      }
    };

    const handleMouseUp = () => {
      isDraggingRef.current = false;
      document.body.style.cursor = '';
      document.body.style.userSelect = '';
    };

    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
    return () => {
      document.removeEventListener('mousemove', handleMouseMove);
      document.removeEventListener('mouseup', handleMouseUp);
    };
  }, []);

  return (
    <div style={styles.container}>
      <header style={styles.header}>
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <span
            style={styles.title}
            onClick={() => { clearPlan(); setMode('view'); }}
            onMouseEnter={(e) => (e.currentTarget.style.color = dracula.purple)}
            onMouseLeave={(e) => (e.currentTarget.style.color = dracula.foreground)}
          >
            Plan Visualizer
          </span>
          {fileName && <span style={styles.fileName}>{fileName}</span>}
          {mode === 'edit' && !fileName && <span style={styles.fileName}>New Plan</span>}
        </div>
        {mode === 'edit' && (
          <button
            style={styles.backButton}
            onClick={() => { clearPlan(); setMode('view'); }}
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
        {mode === 'edit' ? (
          // Edit mode: Editor on left, Canvas on right
          <div ref={editContainerRef} style={{ display: 'flex', flex: 1, overflow: 'hidden' }}>
            <div style={{ width: `${editorWidth}%`, height: '100%', flexShrink: 0, overflow: 'hidden' }}>
              <EditorPanel />
            </div>
            <div
              style={{
                width: '6px',
                height: '100%',
                background: dracula.border,
                cursor: 'col-resize',
                flexShrink: 0,
                transition: 'background 0.2s',
              }}
              onMouseDown={handleEditorDividerMouseDown}
              onMouseEnter={(e) => (e.currentTarget.style.background = dracula.purple)}
              onMouseLeave={(e) => (e.currentTarget.style.background = dracula.border)}
            />
            <div style={{ flex: 1, position: 'relative', minWidth: 0, overflow: 'hidden' }}>
              {planJson ? (
                <>
                  <Canvas />
                  <Toolbar />
                </>
              ) : (
                <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', height: '100%', color: dracula.comment }}>
                  Compile a plan to see the DAG
                </div>
              )}
            </div>
            <DetailsPanel />
          </div>
        ) : (
          // View mode: Original layout
          <>
            <div style={styles.canvasContainer}>
              {planJson ? (
                <>
                  <div ref={containerRef} style={{ display: 'flex', width: '100%', height: '100%', overflow: 'hidden' }}>
                    {showSource && (
                      <>
                        <div style={{ width: `${sourceWidth}%`, height: '100%', flexShrink: 0, overflow: 'hidden' }}>
                          <SourcePanel />
                        </div>
                        <div
                          style={{
                            width: '6px',
                            height: '100%',
                            background: dracula.border,
                            cursor: 'col-resize',
                            flexShrink: 0,
                            transition: 'background 0.2s',
                          }}
                          onMouseDown={handleSourceDividerMouseDown}
                          onMouseEnter={(e) => (e.currentTarget.style.background = dracula.purple)}
                          onMouseLeave={(e) => (e.currentTarget.style.background = dracula.border)}
                        />
                      </>
                    )}
                    <div style={{ flex: 1, height: '100%', position: 'relative', minWidth: 0, overflow: 'hidden' }}>
                      <Canvas />
                    </div>
                  </div>
                  <Toolbar />
                </>
              ) : (
                <PlanSelector onCreateNew={() => setMode('edit')} />
              )}
            </div>
            <DetailsPanel />
          </>
        )}
      </main>
    </div>
  );
}
