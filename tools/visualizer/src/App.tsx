import { useState, useCallback, useRef, useEffect } from 'react';
import { useStore } from './state/store';
import DockLayout, { DockLayoutHandle } from './components/DockLayout';
import PlanSelector from './components/PlanSelector';
import RegistryViewer from './components/RegistryViewer';
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
  projectPath: {
    fontSize: '10px',
    color: dracula.comment,
    opacity: 0.6,
    fontFamily: 'ui-monospace, monospace',
    marginLeft: '12px',
    padding: '2px 6px',
    background: dracula.currentLine,
    borderRadius: '3px',
  },
  navbar: {
    display: 'flex',
    alignItems: 'center',
    gap: '2px',
    marginLeft: '24px',
    padding: '2px',
    background: dracula.currentLine,
    borderRadius: '6px',
  },
  navItem: {
    padding: '6px 14px',
    fontSize: '12px',
    fontWeight: 500,
    color: dracula.comment,
    background: 'transparent',
    border: 'none',
    borderRadius: '4px',
    cursor: 'pointer',
    transition: 'all 0.15s',
  },
  navItemActive: {
    background: dracula.background,
    color: dracula.foreground,
    boxShadow: '0 1px 2px rgba(0,0,0,0.1)',
  },
};

type AppView = 'home' | 'plan' | 'registries';

export default function App() {
  const fileName = useStore((s) => s.fileName);
  const planJson = useStore((s) => s.planJson);
  const sourceCode = useStore((s) => s.sourceCode);
  const clearPlan = useStore((s) => s.clearPlan);
  const loadRegistries = useStore((s) => s.loadRegistries);
  const registries = useStore((s) => s.registries);
  const setEditSourceCode = useStore((s) => s.setEditSourceCode);

  const [mode, setMode] = useState<'view' | 'edit'>('view');
  const [currentView, setCurrentView] = useState<AppView>('home');
  const [existingPanels, setExistingPanels] = useState<Set<string>>(new Set());
  const dockLayoutRef = useRef<DockLayoutHandle>(null);

  // Check URL on mount and handle popstate for registry view
  useEffect(() => {
    const updateViewFromUrl = () => {
      const params = new URLSearchParams(window.location.search);
      if (params.get('view') === 'registries') {
        setCurrentView('registries');
      } else if (params.get('plan')) {
        setCurrentView('plan');
      } else {
        setCurrentView('home');
      }
    };

    // Initial check
    updateViewFromUrl();

    // Listen for popstate (browser back/forward)
    const handlePopState = () => {
      updateViewFromUrl();
    };

    window.addEventListener('popstate', handlePopState);
    return () => window.removeEventListener('popstate', handlePopState);
  }, []);

  // Sync view state with plan changes
  useEffect(() => {
    if (planJson) {
      setCurrentView('plan');
    }
  }, [planJson]);

  const handleGoHome = useCallback(() => {
    clearPlan();
    setEditSourceCode(null);
    setMode('view');
    setCurrentView('home');
    // Update URL
    history.pushState({}, '', window.location.pathname);
  }, [clearPlan, setEditSourceCode]);

  const handleCreateNew = useCallback(() => {
    setEditSourceCode(null);
    setMode('edit');
    setCurrentView('plan');
  }, [setEditSourceCode]);

  // Edit current plan (from toolbar when viewing a plan)
  const handleEditCurrent = useCallback(() => {
    if (sourceCode) {
      setEditSourceCode(sourceCode);
      setMode('edit');
      setCurrentView('plan');
    }
  }, [sourceCode, setEditSourceCode]);

  // Edit a plan by name (from PlanSelector)
  const handleEditPlanByName = useCallback(async (planName: string) => {
    // Fetch the source code directly
    try {
      const resp = await fetch(`/sources/${planName}.plan.ts`);
      if (resp.ok) {
        const code = await resp.text();
        setEditSourceCode(code);
        setMode('edit');
        setCurrentView('plan');
      } else {
        // No source available, just open editor with default
        setEditSourceCode(null);
        setMode('edit');
        setCurrentView('plan');
      }
    } catch {
      setEditSourceCode(null);
      setMode('edit');
      setCurrentView('plan');
    }
  }, [setEditSourceCode]);

  const handleBrowseRegistries = useCallback(() => {
    clearPlan();
    setMode('view');
    setCurrentView('registries');
    if (!registries) {
      loadRegistries();
    }
    // Update URL
    history.pushState({ view: 'registries' }, '', '?view=registries&tab=keys');
  }, [clearPlan, loadRegistries, registries]);

  const handleBrowsePlans = useCallback(() => {
    clearPlan();
    setMode('view');
    setCurrentView('home');
    // Update URL
    history.pushState({}, '', window.location.pathname);
  }, [clearPlan]);

  const handleAddPanel = useCallback((panel: 'editor' | 'canvas' | 'details' | 'source') => {
    dockLayoutRef.current?.addPanel(panel);
  }, []);

  const handleResetLayout = useCallback(() => {
    dockLayoutRef.current?.resetLayout();
  }, []);

  const handlePanelsChange = useCallback((panels: Set<string>) => {
    setExistingPanels(panels);
  }, []);

  // Show simple home page when in view mode with no plan and not in registries view
  const showHomePage = currentView === 'home' && mode === 'view' && !planJson;
  const showRegistries = currentView === 'registries';
  const showPlanView = !showHomePage && !showRegistries;

  // Determine which nav item is active
  const activeNav = showRegistries ? 'registries' : mode === 'edit' ? 'editor' : 'plans';

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
          <span style={styles.projectPath} title={__PROJECT_ROOT__}>
            {__PROJECT_NAME__}
          </span>

          {/* Navigation Bar */}
          <nav style={styles.navbar}>
            <button
              style={{
                ...styles.navItem,
                ...(activeNav === 'editor' ? styles.navItemActive : {}),
              }}
              onClick={handleCreateNew}
              onMouseEnter={(e) => {
                if (activeNav !== 'editor') e.currentTarget.style.color = dracula.foreground;
              }}
              onMouseLeave={(e) => {
                if (activeNav !== 'editor') e.currentTarget.style.color = dracula.comment;
              }}
            >
              Editor
            </button>
            <button
              style={{
                ...styles.navItem,
                ...(activeNav === 'plans' ? styles.navItemActive : {}),
              }}
              onClick={handleBrowsePlans}
              onMouseEnter={(e) => {
                if (activeNav !== 'plans') e.currentTarget.style.color = dracula.foreground;
              }}
              onMouseLeave={(e) => {
                if (activeNav !== 'plans') e.currentTarget.style.color = dracula.comment;
              }}
            >
              Plans
            </button>
            <button
              style={{
                ...styles.navItem,
                ...(activeNav === 'registries' ? styles.navItemActive : {}),
              }}
              onClick={handleBrowseRegistries}
              onMouseEnter={(e) => {
                if (activeNav !== 'registries') e.currentTarget.style.color = dracula.foreground;
              }}
              onMouseLeave={(e) => {
                if (activeNav !== 'registries') e.currentTarget.style.color = dracula.comment;
              }}
            >
              Registries
            </button>
          </nav>

          {/* Context info */}
          {fileName && <span style={styles.fileName}>{fileName}</span>}
          {mode === 'edit' && !fileName && <span style={styles.fileName}>New Plan</span>}

          {/* Menu bar for plan view */}
          {showPlanView && (
            <MenuBar
              onAddPanel={handleAddPanel}
              onResetLayout={handleResetLayout}
              existingPanels={existingPanels}
              mode={mode}
            />
          )}
        </div>
      </header>
      <main style={styles.main}>
        {showHomePage && (
          <PlanSelector
            onCreateNew={handleCreateNew}
            onBrowseRegistries={handleBrowseRegistries}
            onEditPlan={handleEditPlanByName}
          />
        )}
        {showRegistries && <RegistryViewer />}
        {showPlanView && (
          <DockLayout
            ref={dockLayoutRef}
            mode={mode}
            onPanelsChange={handlePanelsChange}
            onEdit={mode === 'view' ? handleEditCurrent : undefined}
          />
        )}
      </main>
    </div>
  );
}
