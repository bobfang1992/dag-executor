import { useEffect, useCallback, useMemo, useState, useRef } from 'react';
import { useStore } from '../state/store';
import type { RegistryTab, EndpointEnv, KeyEntry, ParamEntry, FeatureEntry, CapabilityEntry, TaskEntry, EndpointEntry } from '../types';
import { dracula } from '../theme';

// Type for an open details panel
interface OpenPanel {
  id: string | number;
  tab: RegistryTab;
  x: number;
  y: number;
  zIndex: number;
}

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    flexDirection: 'column',
    width: '100%',
    height: '100%',
    background: dracula.background,
    overflow: 'hidden',
  },
  header: {
    padding: '16px 24px',
    borderBottom: `1px solid ${dracula.border}`,
  },
  searchRow: {
    display: 'flex',
    alignItems: 'center',
    gap: '16px',
    marginBottom: '16px',
  },
  searchInput: {
    flex: 1,
    maxWidth: '400px',
    padding: '10px 14px',
    border: `1px solid ${dracula.border}`,
    borderRadius: '8px',
    fontSize: '14px',
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
    background: dracula.background,
    color: dracula.foreground,
    outline: 'none',
  },
  tabBar: {
    display: 'flex',
    gap: '4px',
  },
  tab: {
    padding: '8px 16px',
    border: '1px solid transparent',
    borderRadius: '6px',
    background: dracula.currentLine,
    color: dracula.comment,
    fontSize: '13px',
    fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
    fontWeight: 500,
    cursor: 'pointer',
    transition: 'all 0.15s',
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
  },
  tabCount: {
    fontSize: '11px',
    opacity: 0.7,
    fontWeight: 400,
  },
  content: {
    flex: 1,
    overflow: 'auto',
    padding: '16px 24px',
  },
  table: {
    width: '100%',
    borderCollapse: 'collapse',
    fontSize: '13px',
  },
  th: {
    textAlign: 'left',
    padding: '10px 12px',
    borderBottom: `2px solid ${dracula.border}`,
    color: dracula.comment,
    fontWeight: 600,
    textTransform: 'uppercase',
    fontSize: '11px',
    letterSpacing: '0.5px',
    cursor: 'pointer',
    userSelect: 'none',
    whiteSpace: 'nowrap',
  },
  td: {
    padding: '10px 12px',
    borderBottom: `1px solid ${dracula.border}`,
    color: dracula.foreground,
    verticalAlign: 'top',
  },
  tr: {
    cursor: 'pointer',
    transition: 'background 0.1s',
  },
  trSelected: {
    background: dracula.selected,
  },
  statusBadge: {
    display: 'inline-block',
    padding: '2px 8px',
    borderRadius: '4px',
    fontSize: '11px',
    fontWeight: 600,
    textTransform: 'uppercase',
  },
  idCell: {
    fontFamily: 'ui-monospace, monospace',
    color: dracula.purple,
    fontWeight: 500,
  },
  nameCell: {
    fontFamily: 'ui-monospace, monospace',
    fontWeight: 500,
  },
  typeCell: {
    fontFamily: 'ui-monospace, monospace',
    color: dracula.cyan,
  },
  docCell: {
    color: dracula.comment,
    maxWidth: '400px',
  },
  boolCell: {
    fontFamily: 'ui-monospace, monospace',
  },
  loading: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    height: '200px',
    color: dracula.comment,
    fontSize: '14px',
  },
  error: {
    padding: '16px',
    background: 'rgba(255, 85, 85, 0.1)',
    border: `1px solid ${dracula.red}`,
    borderRadius: '8px',
    color: dracula.red,
    fontSize: '14px',
  },
  emptyState: {
    padding: '40px',
    textAlign: 'center',
    color: dracula.comment,
    fontSize: '14px',
  },
  envSelector: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    marginTop: '12px',
  },
  envLabel: {
    fontSize: '12px',
    color: dracula.comment,
    fontWeight: 500,
  },
  envButtons: {
    display: 'flex',
    gap: '4px',
  },
  envButton: {
    padding: '4px 12px',
    border: `1px solid ${dracula.border}`,
    borderRadius: '4px',
    background: dracula.currentLine,
    color: dracula.comment,
    fontSize: '12px',
    fontWeight: 500,
    cursor: 'pointer',
    transition: 'all 0.15s',
  },
  detailsPanel: {
    position: 'fixed',
    width: '320px',
    maxHeight: 'calc(100vh - 120px)',
    background: dracula.background,
    border: `1px solid ${dracula.border}`,
    borderRadius: '8px',
    boxShadow: '0 4px 16px rgba(0, 0, 0, 0.15)',
    overflow: 'auto',
  },
  detailsHeader: {
    padding: '10px 12px',
    borderBottom: `1px solid ${dracula.border}`,
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    position: 'sticky',
    top: 0,
    background: dracula.background,
    cursor: 'grab',
    userSelect: 'none',
  },
  detailsHeaderDragging: {
    cursor: 'grabbing',
  },
  detailsTitle: {
    fontSize: '14px',
    fontWeight: 600,
    fontFamily: 'ui-monospace, monospace',
  },
  closeButton: {
    padding: '4px 8px',
    border: 'none',
    background: 'transparent',
    color: dracula.comment,
    cursor: 'pointer',
    fontSize: '16px',
  },
  detailsContent: {
    padding: '16px',
  },
  detailsRow: {
    marginBottom: '12px',
  },
  detailsLabel: {
    fontSize: '11px',
    color: dracula.comment,
    textTransform: 'uppercase',
    marginBottom: '4px',
  },
  detailsValue: {
    fontSize: '13px',
    color: dracula.foreground,
    fontFamily: 'ui-monospace, monospace',
    wordBreak: 'break-word',
  },
  detailsCode: {
    fontFamily: 'ui-monospace, monospace',
    fontSize: '11px',
    background: dracula.currentLine,
    padding: '8px',
    borderRadius: '4px',
    whiteSpace: 'pre-wrap',
    wordBreak: 'break-all',
    border: `1px solid ${dracula.border}`,
  },
};

// Tab colors for each registry type
const tabColors: Record<string, { bg: string; text: string; border: string }> = {
  keys: { bg: '#e8f4fd', text: '#0066cc', border: '#0066cc' },
  params: { bg: '#fff3e6', text: '#cc6600', border: '#cc6600' },
  features: { bg: '#e8f8e8', text: '#228b22', border: '#228b22' },
  capabilities: { bg: '#f3e8fd', text: '#7b2cbf', border: '#7b2cbf' },
  tasks: { bg: '#ffe8ec', text: '#cc0033', border: '#cc0033' },
  endpoints: { bg: '#e8f0f0', text: '#008080', border: '#008080' },
};

function getStatusStyle(status: string): React.CSSProperties {
  switch (status) {
    case 'active':
    case 'implemented':
      return { background: dracula.statusActive, color: dracula.statusActiveText };
    case 'deprecated':
      return { background: dracula.statusDeprecated, color: dracula.statusDeprecatedText };
    case 'draft':
      return { background: dracula.statusDraft, color: dracula.statusDraftText };
    case 'blocked':
      return { background: dracula.statusBlocked, color: dracula.statusBlockedText };
    default:
      return { background: dracula.comment, color: '#ffffff' };
  }
}

function StatusBadge({ status }: { status: string }) {
  return (
    <span style={{ ...styles.statusBadge, ...getStatusStyle(status) }}>
      {status}
    </span>
  );
}

// Draggable details panel component
interface DraggablePanelProps {
  panel: OpenPanel;
  title: string;
  children: React.ReactNode;
  onClose: () => void;
  onDragStart: () => void;
  onDrag: (dx: number, dy: number) => void;
  onDragEnd: () => void;
}

function DraggableDetailsPanel({ panel, title, children, onClose, onDragStart, onDrag, onDragEnd }: DraggablePanelProps) {
  const [isDragging, setIsDragging] = useState(false);
  const dragStartRef = useRef<{ x: number; y: number } | null>(null);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    // Only start drag from the header, not the close button
    if ((e.target as HTMLElement).closest('button')) return;

    e.preventDefault();
    setIsDragging(true);
    dragStartRef.current = { x: e.clientX, y: e.clientY };
    onDragStart();

    const handleMouseMove = (moveEvent: MouseEvent) => {
      if (dragStartRef.current) {
        const dx = moveEvent.clientX - dragStartRef.current.x;
        const dy = moveEvent.clientY - dragStartRef.current.y;
        dragStartRef.current = { x: moveEvent.clientX, y: moveEvent.clientY };
        onDrag(dx, dy);
      }
    };

    const handleMouseUp = () => {
      setIsDragging(false);
      dragStartRef.current = null;
      onDragEnd();
      document.removeEventListener('mousemove', handleMouseMove);
      document.removeEventListener('mouseup', handleMouseUp);
    };

    document.addEventListener('mousemove', handleMouseMove);
    document.addEventListener('mouseup', handleMouseUp);
  }, [onDragStart, onDrag, onDragEnd]);

  return (
    <div
      style={{
        ...styles.detailsPanel,
        left: panel.x,
        top: panel.y,
        zIndex: panel.zIndex,
      }}
    >
      <div
        style={{
          ...styles.detailsHeader,
          ...(isDragging ? styles.detailsHeaderDragging : {}),
        }}
        onMouseDown={handleMouseDown}
      >
        <span style={styles.detailsTitle}>{title}</span>
        <button
          style={styles.closeButton}
          onClick={onClose}
          onMouseEnter={(e) => e.currentTarget.style.color = dracula.foreground}
          onMouseLeave={(e) => e.currentTarget.style.color = dracula.comment}
        >
          ×
        </button>
      </div>
      <div style={styles.detailsContent}>
        {children}
      </div>
    </div>
  );
}

export default function RegistryViewer() {
  const registries = useStore((s) => s.registries);
  const loading = useStore((s) => s.registriesLoading);
  const error = useStore((s) => s.registriesError);
  const selectedTab = useStore((s) => s.selectedRegistryTab);
  const searchQuery = useStore((s) => s.registrySearchQuery);
  const selectedRegistryEntry = useStore((s) => s.selectedRegistryEntry);
  const selectedEndpointEnv = useStore((s) => s.selectedEndpointEnv);
  const loadRegistries = useStore((s) => s.loadRegistries);
  const setRegistryTab = useStore((s) => s.setRegistryTab);
  const setRegistrySearchQuery = useStore((s) => s.setRegistrySearchQuery);
  const setSelectedRegistryEntry = useStore((s) => s.setSelectedRegistryEntry);
  const setSelectedEndpointEnv = useStore((s) => s.setSelectedEndpointEnv);

  // Local state for multiple open panels
  const [openPanels, setOpenPanels] = useState<OpenPanel[]>([]);
  const [nextZIndex, setNextZIndex] = useState(100);

  useEffect(() => {
    if (!registries && !loading && !error) {
      loadRegistries();
    }
  }, [registries, loading, error, loadRegistries]);

  // Auto-open panel when selectedRegistryEntry is set from URL
  useEffect(() => {
    if (selectedRegistryEntry !== null && registries) {
      // Check if panel is already open
      const alreadyOpen = openPanels.some(
        p => p.id === selectedRegistryEntry && p.tab === selectedTab
      );
      if (!alreadyOpen) {
        const offset = (openPanels.length % 5) * 30;
        const newPanel: OpenPanel = {
          id: selectedRegistryEntry,
          tab: selectedTab,
          x: window.innerWidth - 360 - offset,
          y: 80 + offset,
          zIndex: nextZIndex,
        };
        setNextZIndex(z => z + 1);
        setOpenPanels(prev => [...prev, newPanel]);
      }
    }
  }, [selectedRegistryEntry, selectedTab, registries]); // eslint-disable-line react-hooks/exhaustive-deps

  const tabs: { id: RegistryTab; label: string; count: number }[] = useMemo(() => [
    { id: 'keys', label: 'Keys', count: registries?.keys.length ?? 0 },
    { id: 'params', label: 'Params', count: registries?.params.length ?? 0 },
    { id: 'features', label: 'Features', count: registries?.features.length ?? 0 },
    { id: 'capabilities', label: 'Capabilities', count: registries?.capabilities.length ?? 0 },
    { id: 'tasks', label: 'Tasks', count: registries?.tasks.length ?? 0 },
    { id: 'endpoints', label: 'Endpoints', count: registries?.endpoints[selectedEndpointEnv].length ?? 0 },
  ], [registries, selectedEndpointEnv]);

  const filteredData = useMemo(() => {
    if (!registries) return [];
    const query = searchQuery.toLowerCase();

    const filterFn = <T extends { name: string; doc?: string }>(items: T[]): T[] => {
      if (!query) return items;
      return items.filter(item =>
        item.name.toLowerCase().includes(query) ||
        (item.doc && item.doc.toLowerCase().includes(query))
      );
    };

    switch (selectedTab) {
      case 'keys':
        return filterFn(registries.keys);
      case 'params':
        return filterFn(registries.params);
      case 'features':
        return filterFn(registries.features);
      case 'capabilities':
        return registries.capabilities.filter(c =>
          !query || c.name.toLowerCase().includes(query) || c.id.toLowerCase().includes(query) || c.doc.toLowerCase().includes(query)
        );
      case 'tasks':
        return registries.tasks.filter(t =>
          !query || t.op.toLowerCase().includes(query) || t.output_pattern.toLowerCase().includes(query)
        );
      case 'endpoints':
        return registries.endpoints[selectedEndpointEnv].filter(e =>
          !query || e.name.toLowerCase().includes(query) || e.endpoint_id.toLowerCase().includes(query) || e.kind.toLowerCase().includes(query)
        );
      default:
        return [];
    }
  }, [registries, selectedTab, searchQuery, selectedEndpointEnv]);

  // Open a new panel or bring existing to front
  const handleRowClick = useCallback((id: string | number) => {
    // Update URL with selected entry
    setSelectedRegistryEntry(id);

    setOpenPanels(prev => {
      // Check if panel is already open
      const existing = prev.find(p => p.id === id && p.tab === selectedTab);
      if (existing) {
        // Bring to front
        setNextZIndex(z => z + 1);
        return prev.map(p =>
          p.id === id && p.tab === selectedTab
            ? { ...p, zIndex: nextZIndex }
            : p
        );
      }

      // Open new panel with offset based on number of open panels
      const offset = (prev.length % 5) * 30;
      const newPanel: OpenPanel = {
        id,
        tab: selectedTab,
        x: window.innerWidth - 360 - offset,
        y: 80 + offset,
        zIndex: nextZIndex,
      };
      setNextZIndex(z => z + 1);
      return [...prev, newPanel];
    });
  }, [selectedTab, nextZIndex, setSelectedRegistryEntry]);

  // Close a panel
  const handleClosePanel = useCallback((id: string | number, tab: RegistryTab) => {
    setOpenPanels(prev => prev.filter(p => !(p.id === id && p.tab === tab)));
  }, []);

  // Bring panel to front when starting drag
  const handlePanelDragStart = useCallback((id: string | number, tab: RegistryTab) => {
    setOpenPanels(prev => {
      setNextZIndex(z => z + 1);
      return prev.map(p =>
        p.id === id && p.tab === tab
          ? { ...p, zIndex: nextZIndex }
          : p
      );
    });
  }, [nextZIndex]);

  // Update panel position during drag
  const handlePanelDrag = useCallback((id: string | number, tab: RegistryTab, dx: number, dy: number) => {
    setOpenPanels(prev =>
      prev.map(p =>
        p.id === id && p.tab === tab
          ? { ...p, x: p.x + dx, y: p.y + dy }
          : p
      )
    );
  }, []);

  // Get details for a panel
  const getPanelDetails = useCallback((panel: OpenPanel) => {
    if (!registries) return null;

    switch (panel.tab) {
      case 'keys':
        return registries.keys.find(k => k.key_id === panel.id);
      case 'params':
        return registries.params.find(p => p.param_id === panel.id);
      case 'features':
        return registries.features.find(f => f.feature_id === panel.id);
      case 'capabilities':
        return registries.capabilities.find(c => c.id === panel.id);
      case 'tasks':
        return registries.tasks.find(t => t.op === panel.id);
      case 'endpoints':
        return registries.endpoints[selectedEndpointEnv].find(e => e.endpoint_id === panel.id);
      default:
        return null;
    }
  }, [registries, selectedEndpointEnv]);

  // Get title for a panel
  const getPanelTitle = useCallback((panel: OpenPanel) => {
    const details = getPanelDetails(panel);
    if (!details) return String(panel.id);

    switch (panel.tab) {
      case 'keys':
        return (details as KeyEntry).name;
      case 'params':
        return (details as ParamEntry).name;
      case 'features':
        return (details as FeatureEntry).name;
      case 'capabilities':
        return (details as CapabilityEntry).name;
      case 'tasks':
        return (details as TaskEntry).op;
      case 'endpoints':
        return (details as EndpointEntry).name;
      default:
        return String(panel.id);
    }
  }, [getPanelDetails]);

  // Check if an entry has an open panel
  const isPanelOpen = useCallback((id: string | number) => {
    return openPanels.some(p => p.id === id && p.tab === selectedTab);
  }, [openPanels, selectedTab]);

  if (loading) {
    return (
      <div style={styles.container}>
        <div style={styles.loading}>Loading registries...</div>
      </div>
    );
  }

  if (error) {
    return (
      <div style={styles.container}>
        <div style={{ padding: '24px' }}>
          <div style={styles.error}>{error}</div>
        </div>
      </div>
    );
  }

  return (
    <div style={styles.container}>
      <div style={styles.header}>
        <div style={styles.searchRow}>
          <input
            type="text"
            placeholder="Search by name or description..."
            value={searchQuery}
            onChange={(e) => setRegistrySearchQuery(e.target.value)}
            style={styles.searchInput}
            onFocus={(e) => e.currentTarget.style.borderColor = dracula.purple}
            onBlur={(e) => e.currentTarget.style.borderColor = dracula.border}
          />
        </div>
        <div style={styles.tabBar}>
          {tabs.map(tab => {
            const colors = tabColors[tab.id];
            const isActive = selectedTab === tab.id;
            return (
              <button
                key={tab.id}
                style={{
                  ...styles.tab,
                  ...(isActive ? {
                    background: colors.bg,
                    color: colors.text,
                    borderColor: colors.border,
                  } : {}),
                }}
                onClick={() => setRegistryTab(tab.id)}
                onMouseEnter={(e) => {
                  if (!isActive) {
                    e.currentTarget.style.background = colors.bg;
                    e.currentTarget.style.color = colors.text;
                    e.currentTarget.style.borderColor = colors.border;
                  }
                }}
                onMouseLeave={(e) => {
                  if (!isActive) {
                    e.currentTarget.style.background = dracula.currentLine;
                    e.currentTarget.style.color = dracula.comment;
                    e.currentTarget.style.borderColor = 'transparent';
                  }
                }}
              >
                {tab.label}
                <span style={styles.tabCount}>({tab.count})</span>
              </button>
            );
          })}
        </div>
        {selectedTab === 'endpoints' && (
          <div style={styles.envSelector}>
            <span style={styles.envLabel}>Environment:</span>
            <div style={styles.envButtons}>
              {(['dev', 'test', 'prod'] as EndpointEnv[]).map(env => {
                const isActive = selectedEndpointEnv === env;
                const envColors: Record<EndpointEnv, { bg: string; text: string; border: string }> = {
                  dev: { bg: '#e8f4fd', text: '#0066cc', border: '#0066cc' },
                  test: { bg: '#fff3e6', text: '#cc6600', border: '#cc6600' },
                  prod: { bg: '#ffe8ec', text: '#cc0033', border: '#cc0033' },
                };
                const colors = envColors[env];
                return (
                  <button
                    key={env}
                    style={{
                      ...styles.envButton,
                      ...(isActive ? {
                        background: colors.bg,
                        color: colors.text,
                        borderColor: colors.border,
                      } : {}),
                    }}
                    onClick={() => setSelectedEndpointEnv(env)}
                    onMouseEnter={(e) => {
                      if (!isActive) {
                        e.currentTarget.style.background = colors.bg;
                        e.currentTarget.style.color = colors.text;
                        e.currentTarget.style.borderColor = colors.border;
                      }
                    }}
                    onMouseLeave={(e) => {
                      if (!isActive) {
                        e.currentTarget.style.background = dracula.currentLine;
                        e.currentTarget.style.color = dracula.comment;
                        e.currentTarget.style.borderColor = dracula.border;
                      }
                    }}
                  >
                    {env.toUpperCase()}
                  </button>
                );
              })}
            </div>
          </div>
        )}
      </div>

      <div style={styles.content}>
        {filteredData.length === 0 ? (
          <div style={styles.emptyState}>
            {searchQuery ? 'No results found' : 'No entries'}
          </div>
        ) : (
          <table style={styles.table}>
            <thead>
              {selectedTab === 'keys' && (
                <tr>
                  <th style={styles.th}>ID</th>
                  <th style={styles.th}>Name</th>
                  <th style={styles.th}>Type</th>
                  <th style={styles.th}>Nullable</th>
                  <th style={styles.th}>Status</th>
                  <th style={styles.th}>Description</th>
                </tr>
              )}
              {selectedTab === 'params' && (
                <tr>
                  <th style={styles.th}>ID</th>
                  <th style={styles.th}>Name</th>
                  <th style={styles.th}>Type</th>
                  <th style={styles.th}>Nullable</th>
                  <th style={styles.th}>Status</th>
                  <th style={styles.th}>Description</th>
                </tr>
              )}
              {selectedTab === 'features' && (
                <tr>
                  <th style={styles.th}>ID</th>
                  <th style={styles.th}>Stage</th>
                  <th style={styles.th}>Name</th>
                  <th style={styles.th}>Type</th>
                  <th style={styles.th}>Status</th>
                  <th style={styles.th}>Description</th>
                </tr>
              )}
              {selectedTab === 'capabilities' && (
                <tr>
                  <th style={styles.th}>ID</th>
                  <th style={styles.th}>RFC</th>
                  <th style={styles.th}>Name</th>
                  <th style={styles.th}>Status</th>
                  <th style={styles.th}>Description</th>
                </tr>
              )}
              {selectedTab === 'tasks' && (
                <tr>
                  <th style={styles.th}>Op</th>
                  <th style={styles.th}>Output Pattern</th>
                  <th style={styles.th}>Params</th>
                  <th style={styles.th}>Has writes_effect</th>
                </tr>
              )}
              {selectedTab === 'endpoints' && (
                <tr>
                  <th style={styles.th}>ID</th>
                  <th style={styles.th}>Name</th>
                  <th style={styles.th}>Kind</th>
                  <th style={styles.th}>Host:Port</th>
                  <th style={styles.th}>Timeouts</th>
                </tr>
              )}
            </thead>
            <tbody>
              {selectedTab === 'keys' && (filteredData as KeyEntry[]).map(entry => (
                <tr
                  key={entry.key_id}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.key_id) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.key_id)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.key_id)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.key_id)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.idCell }}>{entry.key_id}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.name}</td>
                  <td style={{ ...styles.td, ...styles.typeCell }}>{entry.type}</td>
                  <td style={{ ...styles.td, ...styles.boolCell }}>{entry.nullable ? 'true' : 'false'}</td>
                  <td style={styles.td}><StatusBadge status={entry.status} /></td>
                  <td style={{ ...styles.td, ...styles.docCell }}>{entry.doc}</td>
                </tr>
              ))}
              {selectedTab === 'params' && (filteredData as ParamEntry[]).map(entry => (
                <tr
                  key={entry.param_id}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.param_id) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.param_id)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.param_id)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.param_id)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.idCell }}>{entry.param_id}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.name}</td>
                  <td style={{ ...styles.td, ...styles.typeCell }}>{entry.type}</td>
                  <td style={{ ...styles.td, ...styles.boolCell }}>{entry.nullable ? 'true' : 'false'}</td>
                  <td style={styles.td}><StatusBadge status={entry.status} /></td>
                  <td style={{ ...styles.td, ...styles.docCell }}>{entry.doc}</td>
                </tr>
              ))}
              {selectedTab === 'features' && (filteredData as FeatureEntry[]).map(entry => (
                <tr
                  key={entry.feature_id}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.feature_id) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.feature_id)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.feature_id)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.feature_id)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.idCell }}>{entry.feature_id}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.stage}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.name}</td>
                  <td style={{ ...styles.td, ...styles.typeCell }}>{entry.type}</td>
                  <td style={styles.td}><StatusBadge status={entry.status} /></td>
                  <td style={{ ...styles.td, ...styles.docCell }}>{entry.doc}</td>
                </tr>
              ))}
              {selectedTab === 'capabilities' && (filteredData as CapabilityEntry[]).map(entry => (
                <tr
                  key={entry.id}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.id) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.id)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.id)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.id)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.nameCell, fontSize: '11px' }}>{entry.id}</td>
                  <td style={{ ...styles.td, ...styles.idCell }}>{entry.rfc}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.name}</td>
                  <td style={styles.td}><StatusBadge status={entry.status} /></td>
                  <td style={{ ...styles.td, ...styles.docCell }}>{entry.doc}</td>
                </tr>
              ))}
              {selectedTab === 'tasks' && (filteredData as TaskEntry[]).map(entry => (
                <tr
                  key={entry.op}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.op) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.op)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.op)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.op)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.op}</td>
                  <td style={{ ...styles.td, ...styles.typeCell }}>{entry.output_pattern}</td>
                  <td style={{ ...styles.td, fontSize: '12px' }}>
                    {entry.params.map(p => p.name).join(', ') || '-'}
                  </td>
                  <td style={{ ...styles.td, ...styles.boolCell }}>
                    {entry.writes_effect ? 'yes' : 'no'}
                  </td>
                </tr>
              ))}
              {selectedTab === 'endpoints' && (filteredData as EndpointEntry[]).map(entry => (
                <tr
                  key={entry.endpoint_id}
                  style={{
                    ...styles.tr,
                    ...(isPanelOpen(entry.endpoint_id) ? styles.trSelected : {}),
                  }}
                  onClick={() => handleRowClick(entry.endpoint_id)}
                  onMouseEnter={(e) => {
                    if (!isPanelOpen(entry.endpoint_id)) {
                      e.currentTarget.style.background = dracula.hover;
                    }
                  }}
                  onMouseLeave={(e) => {
                    if (!isPanelOpen(entry.endpoint_id)) {
                      e.currentTarget.style.background = 'transparent';
                    }
                  }}
                >
                  <td style={{ ...styles.td, ...styles.idCell }}>{entry.endpoint_id}</td>
                  <td style={{ ...styles.td, ...styles.nameCell }}>{entry.name}</td>
                  <td style={{ ...styles.td, ...styles.typeCell }}>{entry.kind}</td>
                  <td style={{ ...styles.td, fontFamily: 'ui-monospace, monospace', fontSize: '12px' }}>
                    {entry.resolver.host}:{entry.resolver.port}
                  </td>
                  <td style={{ ...styles.td, fontSize: '11px', color: dracula.comment }}>
                    conn: {entry.policy.connect_timeout_ms}ms, req: {entry.policy.request_timeout_ms}ms
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {/* Details Panels */}
      {openPanels.map(panel => {
        const details = getPanelDetails(panel);
        if (!details) return null;

        return (
          <DraggableDetailsPanel
            key={`${panel.tab}-${panel.id}`}
            panel={panel}
            title={getPanelTitle(panel)}
            onClose={() => handleClosePanel(panel.id, panel.tab)}
            onDragStart={() => handlePanelDragStart(panel.id, panel.tab)}
            onDrag={(dx, dy) => handlePanelDrag(panel.id, panel.tab, dx, dy)}
            onDragEnd={() => {}}
          >
            {panel.tab === 'keys' && renderKeyDetails(details as KeyEntry)}
            {panel.tab === 'params' && renderParamDetails(details as ParamEntry)}
            {panel.tab === 'features' && renderFeatureDetails(details as FeatureEntry)}
            {panel.tab === 'capabilities' && renderCapabilityDetails(details as CapabilityEntry)}
            {panel.tab === 'tasks' && renderTaskDetails(details as TaskEntry)}
            {panel.tab === 'endpoints' && renderEndpointDetails(details as EndpointEntry)}
          </DraggableDetailsPanel>
        );
      })}
    </div>
  );
}

function renderKeyDetails(entry: KeyEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Key ID</div>
        <div style={styles.detailsValue}>{entry.key_id}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Name</div>
        <div style={styles.detailsValue}>{entry.name}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Type</div>
        <div style={styles.detailsValue}>{entry.type}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Status</div>
        <StatusBadge status={entry.status} />
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Nullable</div>
        <div style={styles.detailsValue}>{entry.nullable ? 'true' : 'false'}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Allow Read</div>
        <div style={styles.detailsValue}>{entry.allow_read ? 'true' : 'false'}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Allow Write</div>
        <div style={styles.detailsValue}>{entry.allow_write ? 'true' : 'false'}</div>
      </div>
      {entry.default !== null && (
        <div style={styles.detailsRow}>
          <div style={styles.detailsLabel}>Default</div>
          <div style={styles.detailsValue}>{JSON.stringify(entry.default)}</div>
        </div>
      )}
      {entry.replaced_by !== null && (
        <div style={styles.detailsRow}>
          <div style={styles.detailsLabel}>Replaced By</div>
          <div style={styles.detailsValue}>{entry.replaced_by}</div>
        </div>
      )}
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Description</div>
        <div style={{ ...styles.detailsValue, fontFamily: 'inherit' }}>{entry.doc}</div>
      </div>
    </>
  );
}

function renderParamDetails(entry: ParamEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Param ID</div>
        <div style={styles.detailsValue}>{entry.param_id}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Name</div>
        <div style={styles.detailsValue}>{entry.name}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Type</div>
        <div style={styles.detailsValue}>{entry.type}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Status</div>
        <StatusBadge status={entry.status} />
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Nullable</div>
        <div style={styles.detailsValue}>{entry.nullable ? 'true' : 'false'}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Allow Read</div>
        <div style={styles.detailsValue}>{entry.allow_read ? 'true' : 'false'}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Allow Write</div>
        <div style={styles.detailsValue}>{entry.allow_write ? 'true' : 'false'}</div>
      </div>
      {entry.replaced_by !== null && (
        <div style={styles.detailsRow}>
          <div style={styles.detailsLabel}>Replaced By</div>
          <div style={styles.detailsValue}>{entry.replaced_by}</div>
        </div>
      )}
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Description</div>
        <div style={{ ...styles.detailsValue, fontFamily: 'inherit' }}>{entry.doc}</div>
      </div>
    </>
  );
}

function renderFeatureDetails(entry: FeatureEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Feature ID</div>
        <div style={styles.detailsValue}>{entry.feature_id}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Stage</div>
        <div style={styles.detailsValue}>{entry.stage}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Name</div>
        <div style={styles.detailsValue}>{entry.name}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Type</div>
        <div style={styles.detailsValue}>{entry.type}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Status</div>
        <StatusBadge status={entry.status} />
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Nullable</div>
        <div style={styles.detailsValue}>{entry.nullable ? 'true' : 'false'}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Description</div>
        <div style={{ ...styles.detailsValue, fontFamily: 'inherit' }}>{entry.doc}</div>
      </div>
    </>
  );
}

function renderCapabilityDetails(entry: CapabilityEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Capability ID</div>
        <div style={{ ...styles.detailsValue, fontSize: '11px', wordBreak: 'break-all' }}>{entry.id}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>RFC</div>
        <div style={styles.detailsValue}>{entry.rfc}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Name</div>
        <div style={styles.detailsValue}>{entry.name}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Status</div>
        <StatusBadge status={entry.status} />
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Description</div>
        <div style={{ ...styles.detailsValue, fontFamily: 'inherit' }}>{entry.doc}</div>
      </div>
      {entry.payload_schema && (
        <div style={styles.detailsRow}>
          <div style={styles.detailsLabel}>Payload Schema</div>
          <pre style={styles.detailsCode}>
            {JSON.stringify(entry.payload_schema, null, 2)}
          </pre>
        </div>
      )}
    </>
  );
}

function renderTaskDetails(entry: TaskEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Op</div>
        <div style={styles.detailsValue}>{entry.op}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Output Pattern</div>
        <div style={styles.detailsValue}>{entry.output_pattern}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Parameters</div>
        {entry.params.length > 0 ? (
          <div style={{ marginTop: '4px' }}>
            {entry.params.map(p => (
              <div key={p.name} style={{ marginBottom: '8px', padding: '8px', background: dracula.currentLine, borderRadius: '4px' }}>
                <div style={{ fontFamily: 'ui-monospace, monospace', fontWeight: 500 }}>{p.name}</div>
                <div style={{ fontSize: '11px', color: dracula.comment, marginTop: '2px' }}>
                  type: <span style={{ color: dracula.cyan }}>{p.type}</span>
                  {' | '}
                  required: <span style={{ color: p.required ? dracula.orange : dracula.green }}>{p.required ? 'yes' : 'no'}</span>
                  {' | '}
                  nullable: <span style={{ color: p.nullable ? dracula.orange : dracula.green }}>{p.nullable ? 'yes' : 'no'}</span>
                </div>
              </div>
            ))}
          </div>
        ) : (
          <div style={{ ...styles.detailsValue, color: dracula.comment }}>None</div>
        )}
      </div>
      {entry.writes_effect && (
        <div style={styles.detailsRow}>
          <div style={styles.detailsLabel}>Writes Effect</div>
          <pre style={styles.detailsCode}>
            {JSON.stringify(entry.writes_effect, null, 2)}
          </pre>
        </div>
      )}
    </>
  );
}

function renderEndpointDetails(entry: EndpointEntry) {
  return (
    <>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Endpoint ID</div>
        <div style={styles.detailsValue}>{entry.endpoint_id}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Name</div>
        <div style={styles.detailsValue}>{entry.name}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Kind</div>
        <div style={styles.detailsValue}>{entry.kind}</div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Resolver</div>
        <div style={{ marginTop: '4px', padding: '8px', background: dracula.currentLine, borderRadius: '4px' }}>
          <div style={{ fontSize: '11px', color: dracula.comment }}>
            type: <span style={{ color: dracula.cyan }}>{entry.resolver.type}</span>
          </div>
          <div style={{ fontFamily: 'ui-monospace, monospace', marginTop: '4px' }}>
            {entry.resolver.host}:{entry.resolver.port}
          </div>
        </div>
      </div>
      <div style={styles.detailsRow}>
        <div style={styles.detailsLabel}>Policy</div>
        <div style={{ marginTop: '4px', padding: '8px', background: dracula.currentLine, borderRadius: '4px' }}>
          <div style={{ fontSize: '12px', marginBottom: '4px' }}>
            <span style={{ color: dracula.comment }}>max_inflight:</span>{' '}
            <span style={{ color: dracula.purple }}>{entry.policy.max_inflight}</span>
          </div>
          <div style={{ fontSize: '12px', marginBottom: '4px' }}>
            <span style={{ color: dracula.comment }}>connect_timeout:</span>{' '}
            <span style={{ color: dracula.orange }}>{entry.policy.connect_timeout_ms}ms</span>
          </div>
          <div style={{ fontSize: '12px' }}>
            <span style={{ color: dracula.comment }}>request_timeout:</span>{' '}
            <span style={{ color: dracula.orange }}>{entry.policy.request_timeout_ms}ms</span>
          </div>
        </div>
      </div>
    </>
  );
}
