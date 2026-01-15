import { create } from 'zustand';
import type { PlanJson, PlanIndex, PlanIndexEntry, VisGraph } from '../types';
import { parsePlan } from '../parser/plan-parser';
import { layoutGraph } from '../layout/dagre-layout';

// Base path for artifacts (served via Vite publicDir pointing to artifacts/)
const ARTIFACTS_BASE = '/plans';

// Track if we're handling a popstate to avoid pushing duplicate history
let isHandlingPopState = false;

interface ViewState {
  x: number;
  y: number;
  scale: number;
}

interface Store {
  // Plan index
  planIndex: PlanIndexEntry[] | null;
  indexLoading: boolean;
  indexError: string | null;

  // Plan data
  planJson: PlanJson | null;
  fileName: string | null;
  graph: VisGraph | null;
  planLoading: boolean;
  planError: string | null;

  // Source viewer
  sourceCode: string | null;
  sourceLoading: boolean;
  showSource: boolean;

  // View state
  view: ViewState;
  selectedNodeId: string | null;
  hoveredNodeId: string | null;

  // Fragment state
  expandedFragments: Set<string>;
  autoCollapseThreshold: number;

  // Actions
  loadIndex: () => Promise<void>;
  loadPlanByName: (name: string) => Promise<void>;
  loadPlan: (json: PlanJson, fileName: string) => void;
  clearPlan: () => void;
  toggleSource: () => void;
  loadSource: (planName: string) => Promise<void>;
  setView: (view: Partial<ViewState>) => void;
  resetView: () => void;
  fitToView: (canvasWidth: number, canvasHeight: number) => void;
  selectNode: (nodeId: string | null) => void;
  hoverNode: (nodeId: string | null) => void;
  toggleFragment: (fragmentId: string) => void;
}

const initialView: ViewState = { x: 0, y: 0, scale: 1 };

export const useStore = create<Store>((set, get) => ({
  // Initial state
  planIndex: null,
  indexLoading: false,
  indexError: null,
  planJson: null,
  fileName: null,
  graph: null,
  planLoading: false,
  planError: null,
  sourceCode: null,
  sourceLoading: false,
  showSource: false,
  view: initialView,
  selectedNodeId: null,
  hoveredNodeId: null,
  expandedFragments: new Set(),
  autoCollapseThreshold: 200,

  // Actions
  loadIndex: async () => {
    set({ indexLoading: true, indexError: null });
    try {
      const resp = await fetch(`${ARTIFACTS_BASE}/index.json`);
      if (!resp.ok) throw new Error(`Failed to fetch index: ${resp.status}`);
      const data = (await resp.json()) as PlanIndex;
      set({ planIndex: data.plans, indexLoading: false });
    } catch (e) {
      set({
        indexError: e instanceof Error ? e.message : String(e),
        indexLoading: false,
      });
    }
  },

  loadPlanByName: async (name: string) => {
    const { planIndex } = get();
    const entry = planIndex?.find((p) => p.name === name);
    if (!entry) {
      set({ planError: `Plan not found: ${name}` });
      return;
    }

    set({ planLoading: true, planError: null });
    try {
      const resp = await fetch(`${ARTIFACTS_BASE}/${entry.path}`);
      if (!resp.ok) throw new Error(`Failed to fetch plan: ${resp.status}`);
      const json = (await resp.json()) as PlanJson;
      get().loadPlan(json, entry.path);
      set({ planLoading: false });
      // Also load source
      get().loadSource(name);
    } catch (e) {
      set({
        planError: e instanceof Error ? e.message : String(e),
        planLoading: false,
      });
    }
  },

  loadPlan: (json, fileName) => {
    const parsed = parsePlan(json);
    const graph = layoutGraph(parsed);
    set({
      planJson: json,
      fileName,
      graph,
      selectedNodeId: null,
      hoveredNodeId: null,
      expandedFragments: new Set(),
      planError: null,
    });
    // Push to browser history (unless we're handling a popstate)
    if (!isHandlingPopState) {
      history.pushState({ plan: json.plan_name }, '', `?plan=${json.plan_name}`);
    }
  },

  clearPlan: () => {
    set({
      planJson: null,
      fileName: null,
      graph: null,
      view: initialView,
      selectedNodeId: null,
      hoveredNodeId: null,
      expandedFragments: new Set(),
      sourceCode: null,
      showSource: false,
    });
    // Update URL (unless we're handling a popstate)
    if (!isHandlingPopState) {
      history.pushState({}, '', window.location.pathname);
    }
  },

  toggleSource: () => {
    set((state) => ({ showSource: !state.showSource }));
  },

  loadSource: async (planName: string) => {
    set({ sourceLoading: true });
    try {
      const resp = await fetch(`/sources/${planName}.plan.ts`);
      if (resp.ok) {
        const code = await resp.text();
        set({ sourceCode: code, sourceLoading: false });
      } else {
        set({ sourceCode: null, sourceLoading: false });
      }
    } catch {
      set({ sourceCode: null, sourceLoading: false });
    }
  },

  setView: (partial) => {
    const { view } = get();
    set({ view: { ...view, ...partial } });
  },

  resetView: () => {
    set({ view: initialView });
  },

  fitToView: (canvasWidth, canvasHeight) => {
    const { graph } = get();
    if (!graph || graph.nodes.size === 0) return;

    const nodes = Array.from(graph.nodes.values());
    const minX = Math.min(...nodes.map((n) => n.x));
    const maxX = Math.max(...nodes.map((n) => n.x + n.width));
    const minY = Math.min(...nodes.map((n) => n.y));
    const maxY = Math.max(...nodes.map((n) => n.y + n.height));

    const graphWidth = maxX - minX;
    const graphHeight = maxY - minY;
    const padding = 60;

    const scaleX = (canvasWidth - padding * 2) / graphWidth;
    const scaleY = (canvasHeight - padding * 2) / graphHeight;
    const scale = Math.min(scaleX, scaleY, 2); // Cap at 2x

    const centerX = (minX + maxX) / 2;
    const centerY = (minY + maxY) / 2;

    set({
      view: {
        x: canvasWidth / 2 - centerX * scale,
        y: canvasHeight / 2 - centerY * scale,
        scale,
      },
    });
  },

  selectNode: (nodeId) => {
    set({ selectedNodeId: nodeId });
  },

  hoverNode: (nodeId) => {
    set({ hoveredNodeId: nodeId });
  },

  toggleFragment: (fragmentId) => {
    const { expandedFragments, graph, autoCollapseThreshold } = get();
    const newExpanded = new Set(expandedFragments);

    if (newExpanded.has(fragmentId)) {
      newExpanded.delete(fragmentId);
    } else {
      newExpanded.add(fragmentId);

      // Auto-collapse check (future: when fragments are implemented)
      if (graph) {
        const visibleCount = countVisibleNodes(graph, newExpanded);
        if (visibleCount > autoCollapseThreshold) {
          // Collapse oldest (LRU) - for now just collapse oldest
          const iterator = newExpanded.values();
          while (countVisibleNodes(graph, newExpanded) > autoCollapseThreshold) {
            const oldest = iterator.next().value;
            if (oldest && oldest !== fragmentId) {
              newExpanded.delete(oldest);
            } else {
              break;
            }
          }
        }
      }
    }

    set({ expandedFragments: newExpanded });
  },
}));

function countVisibleNodes(graph: VisGraph, _expanded: Set<string>): number {
  // For now, all nodes are visible (fragments not implemented)
  // _expanded will be used when fragment collapse is implemented
  return graph.nodes.size;
}

// Set up browser history handling
function initHistory() {
  // Handle browser back/forward buttons
  window.addEventListener('popstate', (event) => {
    isHandlingPopState = true;
    try {
      const state = useStore.getState();
      if (event.state?.plan) {
        // Navigate to a plan
        state.loadPlanByName(event.state.plan);
      } else {
        // Navigate back to selector
        state.clearPlan();
      }
    } finally {
      isHandlingPopState = false;
    }
  });

  // Check initial URL for plan param
  const params = new URLSearchParams(window.location.search);
  const planName = params.get('plan');
  if (planName) {
    // Load index first, then load the plan
    const state = useStore.getState();
    state.loadIndex().then(() => {
      isHandlingPopState = true; // Don't push duplicate history
      state.loadPlanByName(planName);
      isHandlingPopState = false;
    });
  }
}

// Initialize on module load
initHistory();
