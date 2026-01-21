import { create } from 'zustand';
import type {
  PlanJson,
  PlanIndex,
  PlanIndexEntry,
  VisGraph,
  RegistryData,
  RegistryTab,
  EndpointEnv,
  KeyEntry,
  ParamEntry,
  FeatureEntry,
  CapabilityEntry,
  TaskEntry,
  EndpointEntry,
} from '../types';
import { parsePlan } from '../parser/plan-parser';
import { layoutGraph } from '../layout/dagre-layout';

// Base path for artifacts (served via Vite publicDir pointing to artifacts/)
const ARTIFACTS_BASE = '/plans';
const REGISTRIES_BASE = '';

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
  sourceCollapsed: boolean;

  // Editor: source code to edit (set when "Edit" is clicked on a plan)
  editSourceCode: string | null;

  // Panel collapse state
  detailsCollapsed: boolean;

  // View state
  view: ViewState;
  selectedNodeId: string | null;
  hoveredNodeId: string | null;

  // Fragment state
  expandedFragments: Set<string>;
  autoCollapseThreshold: number;

  // Registry state
  registries: RegistryData | null;
  registriesLoading: boolean;
  registriesError: string | null;
  selectedRegistryTab: RegistryTab;
  registrySearchQuery: string;
  selectedRegistryEntry: string | number | null;
  selectedEndpointEnv: EndpointEnv;

  // Actions
  loadIndex: () => Promise<void>;
  loadPlanByName: (name: string) => Promise<void>;
  loadPlan: (json: PlanJson, fileName: string, options?: { skipHistory?: boolean }) => void;
  clearPlan: () => void;
  toggleSource: () => void;
  toggleSourceCollapsed: () => void;
  toggleDetailsCollapsed: () => void;
  setEditSourceCode: (code: string | null) => void;
  loadSource: (planName: string) => Promise<void>;
  setView: (view: Partial<ViewState>) => void;
  resetView: () => void;
  fitToView: (canvasWidth: number, canvasHeight: number) => void;
  selectNode: (nodeId: string | null) => void;
  hoverNode: (nodeId: string | null) => void;
  toggleFragment: (fragmentId: string) => void;

  // Registry actions
  loadRegistries: () => Promise<void>;
  setRegistryTab: (tab: RegistryTab) => void;
  setRegistrySearchQuery: (query: string) => void;
  setSelectedRegistryEntry: (id: string | number | null) => void;
  navigateToRegistryEntry: (tab: RegistryTab, id: string | number) => void;
  setSelectedEndpointEnv: (env: EndpointEnv) => void;
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
  sourceCollapsed: false,
  editSourceCode: null,
  detailsCollapsed: false,
  view: initialView,
  selectedNodeId: null,
  hoveredNodeId: null,
  expandedFragments: new Set(),
  autoCollapseThreshold: 200,

  // Registry initial state
  registries: null,
  registriesLoading: false,
  registriesError: null,
  selectedRegistryTab: 'keys',
  registrySearchQuery: '',
  selectedRegistryEntry: null,
  selectedEndpointEnv: 'dev',

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

  loadPlan: (json, fileName, options) => {
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
    // Push to browser history (unless we're handling a popstate or skipHistory is set)
    // skipHistory is used for live editor plans which aren't in the plan index
    if (!isHandlingPopState && !options?.skipHistory) {
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

  toggleSourceCollapsed: () => {
    set((state) => ({ sourceCollapsed: !state.sourceCollapsed }));
  },

  toggleDetailsCollapsed: () => {
    set((state) => ({ detailsCollapsed: !state.detailsCollapsed }));
  },

  setEditSourceCode: (code) => {
    set({ editSourceCode: code });
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

  // Registry actions
  loadRegistries: async () => {
    set({ registriesLoading: true, registriesError: null });
    try {
      const [keysRes, paramsRes, featuresRes, capsRes, tasksRes, endpointsDevRes, endpointsTestRes, endpointsProdRes] = await Promise.all([
        fetch(`${REGISTRIES_BASE}/keys.json`),
        fetch(`${REGISTRIES_BASE}/params.json`),
        fetch(`${REGISTRIES_BASE}/features.json`),
        fetch(`${REGISTRIES_BASE}/capabilities.json`),
        fetch(`${REGISTRIES_BASE}/tasks.json`),
        fetch(`${REGISTRIES_BASE}/endpoints.dev.json`),
        fetch(`${REGISTRIES_BASE}/endpoints.test.json`),
        fetch(`${REGISTRIES_BASE}/endpoints.prod.json`),
      ]);

      if (!keysRes.ok) throw new Error(`Failed to fetch keys: ${keysRes.status}`);
      if (!paramsRes.ok) throw new Error(`Failed to fetch params: ${paramsRes.status}`);
      if (!featuresRes.ok) throw new Error(`Failed to fetch features: ${featuresRes.status}`);
      if (!capsRes.ok) throw new Error(`Failed to fetch capabilities: ${capsRes.status}`);
      if (!tasksRes.ok) throw new Error(`Failed to fetch tasks: ${tasksRes.status}`);
      if (!endpointsDevRes.ok) throw new Error(`Failed to fetch endpoints.dev: ${endpointsDevRes.status}`);
      if (!endpointsTestRes.ok) throw new Error(`Failed to fetch endpoints.test: ${endpointsTestRes.status}`);
      if (!endpointsProdRes.ok) throw new Error(`Failed to fetch endpoints.prod: ${endpointsProdRes.status}`);

      const keysData = (await keysRes.json()) as { entries: KeyEntry[] };
      const paramsData = (await paramsRes.json()) as { entries: ParamEntry[] };
      const featuresData = (await featuresRes.json()) as { entries: FeatureEntry[] };
      const capsData = (await capsRes.json()) as { entries: CapabilityEntry[] };
      const tasksData = (await tasksRes.json()) as { tasks: TaskEntry[] };
      const endpointsDevData = (await endpointsDevRes.json()) as { endpoints: EndpointEntry[] };
      const endpointsTestData = (await endpointsTestRes.json()) as { endpoints: EndpointEntry[] };
      const endpointsProdData = (await endpointsProdRes.json()) as { endpoints: EndpointEntry[] };

      set({
        registries: {
          keys: keysData.entries,
          params: paramsData.entries,
          features: featuresData.entries,
          capabilities: capsData.entries,
          tasks: tasksData.tasks,
          endpoints: {
            dev: endpointsDevData.endpoints,
            test: endpointsTestData.endpoints,
            prod: endpointsProdData.endpoints,
          },
        },
        registriesLoading: false,
      });
    } catch (e) {
      set({
        registriesError: e instanceof Error ? e.message : String(e),
        registriesLoading: false,
      });
    }
  },

  setRegistryTab: (tab) => {
    set({ selectedRegistryTab: tab, selectedRegistryEntry: null });
    // Update URL
    if (!isHandlingPopState) {
      const url = new URL(window.location.href);
      url.searchParams.set('view', 'registries');
      url.searchParams.set('tab', tab);
      url.searchParams.delete('selected');
      history.pushState({ view: 'registries', tab }, '', url.toString());
    }
  },

  setRegistrySearchQuery: (query) => {
    set({ registrySearchQuery: query });
  },

  setSelectedRegistryEntry: (id) => {
    set({ selectedRegistryEntry: id });
    // Update URL
    if (!isHandlingPopState && id !== null) {
      const { selectedRegistryTab } = get();
      const url = new URL(window.location.href);
      url.searchParams.set('view', 'registries');
      url.searchParams.set('tab', selectedRegistryTab);
      url.searchParams.set('selected', String(id));
      history.pushState({ view: 'registries', tab: selectedRegistryTab, selected: id }, '', url.toString());
    }
  },

  navigateToRegistryEntry: (tab, id) => {
    set({
      selectedRegistryTab: tab,
      selectedRegistryEntry: id,
      registrySearchQuery: '',
    });
    // Update URL
    if (!isHandlingPopState) {
      const url = new URL(window.location.href);
      url.searchParams.set('view', 'registries');
      url.searchParams.set('tab', tab);
      url.searchParams.set('selected', String(id));
      history.pushState({ view: 'registries', tab, selected: id }, '', url.toString());
    }
  },

  setSelectedEndpointEnv: (env) => {
    set({ selectedEndpointEnv: env, selectedRegistryEntry: null });
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
  window.addEventListener('popstate', async (event) => {
    isHandlingPopState = true;
    try {
      const state = useStore.getState();
      const params = new URLSearchParams(window.location.search);

      // Check for registry view
      const view = event.state?.view || params.get('view');
      if (view === 'registries') {
        const tab = (event.state?.tab || params.get('tab') || 'keys') as RegistryTab;
        const selected = event.state?.selected || params.get('selected');
        state.clearPlan();
        if (!state.registries) {
          await state.loadRegistries();
        }
        useStore.setState({
          selectedRegistryTab: tab,
          selectedRegistryEntry: selected ? (isNaN(Number(selected)) ? selected : Number(selected)) : null,
        });
        return;
      }

      // Check event.state first, then fall back to URL params
      // (initial page load from ?plan=foo has no state object)
      const planFromState = event.state?.plan;
      const planFromUrl = params.get('plan');
      const planName = planFromState || planFromUrl;

      if (planName) {
        // Navigate to a plan - await to keep flag set during async load
        await state.loadPlanByName(planName);
      } else {
        // Navigate back to selector
        state.clearPlan();
      }
    } finally {
      isHandlingPopState = false;
    }
  });

  // Check initial URL for plan param or registry view
  const params = new URLSearchParams(window.location.search);
  const view = params.get('view');
  const planName = params.get('plan');

  if (view === 'registries') {
    const tab = (params.get('tab') || 'keys') as RegistryTab;
    const selected = params.get('selected');
    const state = useStore.getState();
    isHandlingPopState = true;
    state.loadRegistries().then(() => {
      useStore.setState({
        selectedRegistryTab: tab,
        selectedRegistryEntry: selected ? (isNaN(Number(selected)) ? selected : Number(selected)) : null,
      });
      isHandlingPopState = false;
    });
  } else if (planName) {
    // Load index first, then load the plan
    const state = useStore.getState();
    state.loadIndex().then(async () => {
      isHandlingPopState = true; // Don't push duplicate history
      await state.loadPlanByName(planName);
      isHandlingPopState = false;
    });
  }
}

// Initialize on module load
initHistory();
