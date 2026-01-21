import { useEffect, useRef, useCallback, useState, useImperativeHandle, forwardRef, createContext, useContext } from 'react';
import {
  DockviewReact,
  DockviewApi,
  DockviewReadyEvent,
  IDockviewPanelProps,
} from 'dockview';
import Canvas from './Canvas';
import DetailsPanel from './DetailsPanel';
import EditorPanel from './EditorPanel';
import SourcePanel from './SourcePanel';
import Toolbar from './Toolbar';
import { useStore } from '../state/store';
import { dracula } from '../theme';
import * as prefs from '../state/preferences';

interface DockLayoutProps {
  mode: 'view' | 'edit';
  onPanelsChange?: (panels: Set<string>) => void;
  onEdit?: () => void;
}

// Context to pass onEdit callback to nested components
const EditContext = createContext<(() => void) | undefined>(undefined);

export interface DockLayoutHandle {
  addPanel: (panel: 'editor' | 'canvas' | 'details' | 'source') => void;
  resetLayout: () => void;
  getExistingPanels: () => Set<string>;
}

// Panel wrapper components for dockview
function CanvasPanelContent() {
  const planJson = useStore((s) => s.planJson);
  const onEdit = useContext(EditContext);

  if (!planJson) {
    return (
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        height: '100%',
        color: dracula.comment,
        background: dracula.background,
      }}>
        Compile a plan to see the DAG
      </div>
    );
  }

  return (
    <div style={{ width: '100%', height: '100%', position: 'relative' }}>
      <Canvas />
      <Toolbar onEdit={onEdit} />
    </div>
  );
}

function CanvasPanel(_props: IDockviewPanelProps) {
  return <CanvasPanelContent />;
}

function DetailsPanelWrapper(_props: IDockviewPanelProps) {
  return <DetailsPanel />;
}

function EditorPanelWrapper(_props: IDockviewPanelProps) {
  return <EditorPanel />;
}

function SourcePanelWrapper(_props: IDockviewPanelProps) {
  return <SourcePanel />;
}

// Component map for dockview
const components = {
  canvas: CanvasPanel,
  details: DetailsPanelWrapper,
  editor: EditorPanelWrapper,
  source: SourcePanelWrapper,
};

const panelTitles: Record<string, string> = {
  canvas: 'Canvas',
  details: 'Details',
  editor: 'Editor',
  source: 'Source',
};

// Save panel positions to preferences
// Merges with existing positions to preserve closed panel positions for restore
function savePanelPositions(api: DockviewApi, mode: 'view' | 'edit'): void {
  const existingPositions = prefs.getPanelPositions(mode);
  const positions: Record<string, prefs.PanelPosition> = { ...existingPositions };
  for (const panel of api.panels) {
    const group = panel.group;
    if (group) {
      const index = group.panels.findIndex(p => p.id === panel.id);
      positions[panel.id] = {
        groupId: group.id,
        index: index >= 0 ? index : 0,
      };
    }
  }
  prefs.setPanelPositions(mode, positions);
}

// Build default layout for a mode
function buildDefaultLayout(api: DockviewApi, mode: 'view' | 'edit'): void {
  api.clear();

  if (mode === 'edit') {
    // Edit mode: Editor (40%) | Canvas (35%) | Details (25%)
    api.addPanel({
      id: 'editor',
      component: 'editor',
      title: 'Editor',
    });

    api.addPanel({
      id: 'canvas',
      component: 'canvas',
      title: 'Canvas',
      position: { referencePanel: 'editor', direction: 'right' },
    });

    api.addPanel({
      id: 'details',
      component: 'details',
      title: 'Details',
      position: { referencePanel: 'canvas', direction: 'right' },
    });

    // Set initial sizes
    setTimeout(() => {
      try {
        const editorGroup = api.getPanel('editor')?.group;
        const detailsGroup = api.getPanel('details')?.group;
        if (editorGroup) editorGroup.api.setSize({ width: api.width * 0.4 });
        if (detailsGroup) detailsGroup.api.setSize({ width: api.width * 0.25 });
      } catch {
        // Ignore sizing errors
      }
    }, 0);
  } else {
    // View mode: Canvas (75%) | Details (25%)
    api.addPanel({
      id: 'canvas',
      component: 'canvas',
      title: 'Canvas',
    });

    api.addPanel({
      id: 'details',
      component: 'details',
      title: 'Details',
      position: { referencePanel: 'canvas', direction: 'right' },
    });

    // Set initial sizes
    setTimeout(() => {
      try {
        const detailsGroup = api.getPanel('details')?.group;
        if (detailsGroup) detailsGroup.api.setSize({ width: api.width * 0.25 });
      } catch {
        // Ignore sizing errors
      }
    }, 0);
  }
}

const DockLayout = forwardRef<DockLayoutHandle, DockLayoutProps>(function DockLayout({ mode, onPanelsChange, onEdit }, ref) {
  const apiRef = useRef<DockviewApi | null>(null);
  const initializedModeRef = useRef<string | null>(null);
  const modeRef = useRef(mode); // Track current mode for listeners
  const [existingPanels, setExistingPanels] = useState<Set<string>>(new Set());

  // Keep modeRef in sync with mode prop
  useEffect(() => {
    modeRef.current = mode;
  }, [mode]);

  // Update existing panels set
  const updateExistingPanels = useCallback((api: DockviewApi) => {
    const panels = new Set(api.panels.map(p => p.id));
    setExistingPanels(panels);
    onPanelsChange?.(panels);
  }, [onPanelsChange]);

  // Expose methods to parent
  useImperativeHandle(ref, () => ({
    addPanel: (panelId: 'editor' | 'canvas' | 'details' | 'source') => {
      const api = apiRef.current;
      if (!api || api.getPanel(panelId)) return;

      // Try to restore to saved position
      const savedPositions = prefs.getPanelPositions(mode);
      const savedPos = savedPositions[panelId];

      if (savedPos) {
        // Try to find the group the panel was in
        const group = api.groups.find(g => g.id === savedPos.groupId);
        if (group) {
          // Add to the same group at the same index
          api.addPanel({
            id: panelId,
            component: panelId,
            title: panelTitles[panelId],
            position: { referenceGroup: group.id, index: savedPos.index },
          });
          updateExistingPanels(api);
          prefs.setDockLayout(mode, api.toJSON());
          return;
        }
      }

      // Fallback: find a reference panel to position relative to
      const refPanel = api.panels[0];

      api.addPanel({
        id: panelId,
        component: panelId,
        title: panelTitles[panelId],
        position: refPanel ? { referencePanel: refPanel.id, direction: 'right' } : undefined,
      });

      updateExistingPanels(api);
      prefs.setDockLayout(mode, api.toJSON());
    },
    resetLayout: () => {
      const api = apiRef.current;
      if (!api) return;

      prefs.removeDockLayout(mode);
      prefs.removePanelPositions(mode);
      buildDefaultLayout(api, mode);
      updateExistingPanels(api);
    },
    getExistingPanels: () => existingPanels,
  }), [mode, existingPanels, updateExistingPanels]);

  const handleReady = useCallback((event: DockviewReadyEvent) => {
    const api = event.api;
    apiRef.current = api;

    // Try to restore saved layout, otherwise build default
    const savedLayout = prefs.getDockLayout(mode);
    if (savedLayout) {
      try {
        api.fromJSON(savedLayout);
      } catch {
        buildDefaultLayout(api, mode);
      }
    } else {
      buildDefaultLayout(api, mode);
    }

    initializedModeRef.current = mode;
    updateExistingPanels(api);
    savePanelPositions(api, mode);

    // Save layout and positions on any change
    // Use modeRef.current to always get the current mode (not the closed-over value)
    const layoutDisposable = api.onDidLayoutChange(() => {
      const currentMode = modeRef.current;
      prefs.setDockLayout(currentMode, api.toJSON());
      savePanelPositions(api, currentMode);
      updateExistingPanels(api);
    });

    // Track panel removals
    const removeDisposable = api.onDidRemovePanel(() => {
      updateExistingPanels(api);
    });

    return () => {
      layoutDisposable.dispose();
      removeDisposable.dispose();
    };
  }, [mode, updateExistingPanels]);

  // Handle mode changes
  useEffect(() => {
    const api = apiRef.current;
    if (!api || initializedModeRef.current === mode) return;

    // Mode changed - save current layout and load new one
    if (initializedModeRef.current) {
      prefs.setDockLayout(initializedModeRef.current as 'view' | 'edit', api.toJSON());
    }

    const savedLayout = prefs.getDockLayout(mode);
    if (savedLayout) {
      try {
        api.fromJSON(savedLayout);
      } catch {
        buildDefaultLayout(api, mode);
      }
    } else {
      buildDefaultLayout(api, mode);
    }

    initializedModeRef.current = mode;
    updateExistingPanels(api);
  }, [mode, updateExistingPanels]);

  return (
    <EditContext.Provider value={onEdit}>
      <DockviewReact
        onReady={handleReady}
        components={components}
        className="dockview-theme-light"
      />
    </EditContext.Provider>
  );
});

export default DockLayout;
