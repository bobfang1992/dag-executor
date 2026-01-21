/**
 * User Preferences Module
 * Centralizes all localStorage operations for the visualizer.
 */

import type { SerializedDockview } from 'dockview';

const PREFIX = 'visualizer';

// Storage keys
const KEYS = {
  editorCode: `${PREFIX}:editor:code`,
  savedPlans: `${PREFIX}:saved-plans`,
  dockLayout: (mode: 'view' | 'edit') => `${PREFIX}:dock-layout:${mode}`,
  panelPositions: (mode: 'view' | 'edit') => `${PREFIX}:panel-positions:${mode}`,
} as const;

// ============================================================================
// Generic helpers
// ============================================================================

function get<T>(key: string): T | null {
  try {
    const value = localStorage.getItem(key);
    return value ? JSON.parse(value) : null;
  } catch {
    return null;
  }
}

function set<T>(key: string, value: T): void {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch {
    // Storage full or disabled
  }
}

function remove(key: string): void {
  try {
    localStorage.removeItem(key);
  } catch {
    // Ignore
  }
}

// ============================================================================
// Editor Code
// ============================================================================

export function getEditorCode(): string | null {
  try {
    return localStorage.getItem(KEYS.editorCode);
  } catch {
    return null;
  }
}

export function setEditorCode(code: string): void {
  try {
    localStorage.setItem(KEYS.editorCode, code);
  } catch {
    // Ignore
  }
}

export function removeEditorCode(): void {
  remove(KEYS.editorCode);
}

// ============================================================================
// Saved Plans
// ============================================================================

export interface SavedPlan {
  id: string;
  name: string;
  code: string;
  updatedAt: number;
}

export interface SavedPlansState {
  plans: SavedPlan[];
  currentId: string | null;
}

export function getSavedPlans(): SavedPlansState {
  return get<SavedPlansState>(KEYS.savedPlans) ?? { plans: [], currentId: null };
}

export function setSavedPlans(state: SavedPlansState): void {
  set(KEYS.savedPlans, state);
}

// ============================================================================
// Dock Layout
// ============================================================================

export function getDockLayout(mode: 'view' | 'edit'): SerializedDockview | null {
  return get<SerializedDockview>(KEYS.dockLayout(mode));
}

export function setDockLayout(mode: 'view' | 'edit', layout: SerializedDockview): void {
  set(KEYS.dockLayout(mode), layout);
}

export function removeDockLayout(mode: 'view' | 'edit'): void {
  remove(KEYS.dockLayout(mode));
}

// ============================================================================
// Panel Positions
// ============================================================================

export interface PanelPosition {
  groupId: string;
  index: number;
}

export function getPanelPositions(mode: 'view' | 'edit'): Record<string, PanelPosition> {
  return get<Record<string, PanelPosition>>(KEYS.panelPositions(mode)) ?? {};
}

export function setPanelPositions(mode: 'view' | 'edit', positions: Record<string, PanelPosition>): void {
  set(KEYS.panelPositions(mode), positions);
}

export function removePanelPositions(mode: 'view' | 'edit'): void {
  remove(KEYS.panelPositions(mode));
}

// ============================================================================
// Reset All
// ============================================================================

export function resetAllPreferences(): void {
  remove(KEYS.editorCode);
  remove(KEYS.savedPlans);
  remove(KEYS.dockLayout('view'));
  remove(KEYS.dockLayout('edit'));
  remove(KEYS.panelPositions('view'));
  remove(KEYS.panelPositions('edit'));
}
