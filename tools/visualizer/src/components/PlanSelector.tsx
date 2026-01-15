import { useEffect, useCallback, useState } from 'react';
import { useStore } from '../state/store';
import type { PlanJson } from '../types';
import { dracula } from '../theme';

const styles: Record<string, React.CSSProperties> = {
  container: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    width: '100%',
    height: '100%',
    background: dracula.background,
    padding: '20px',
  },
  title: {
    fontSize: '24px',
    fontWeight: 600,
    marginBottom: '24px',
    color: dracula.foreground,
  },
  subtitle: {
    fontSize: '14px',
    color: dracula.comment,
    marginBottom: '20px',
  },
  planList: {
    display: 'flex',
    flexDirection: 'column',
    gap: '8px',
    width: '100%',
    maxWidth: '400px',
    marginBottom: '24px',
  },
  planButton: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '12px 16px',
    background: dracula.currentLine,
    border: `1px solid ${dracula.border}`,
    borderRadius: '8px',
    color: dracula.foreground,
    fontSize: '14px',
    cursor: 'pointer',
    transition: 'all 0.2s',
    fontFamily: 'ui-monospace, monospace',
  },
  planName: {
    fontWeight: 500,
    color: dracula.foreground,
  },
  planMeta: {
    fontSize: '11px',
    color: dracula.comment,
  },
  divider: {
    display: 'flex',
    alignItems: 'center',
    width: '100%',
    maxWidth: '400px',
    margin: '8px 0',
    color: dracula.comment,
    fontSize: '12px',
  },
  dividerLine: {
    flex: 1,
    height: '1px',
    background: dracula.border,
  },
  dividerText: {
    padding: '0 12px',
  },
  dropZone: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    justifyContent: 'center',
    width: '100%',
    maxWidth: '400px',
    padding: '24px',
    border: `2px dashed ${dracula.comment}`,
    borderRadius: '12px',
    cursor: 'pointer',
    transition: 'all 0.2s',
  },
  dropZoneActive: {
    borderColor: dracula.purple,
    background: 'rgba(189, 147, 249, 0.1)',
  },
  dropText: {
    fontSize: '14px',
    color: dracula.comment,
  },
  error: {
    marginTop: '16px',
    padding: '8px 16px',
    background: 'rgba(255, 85, 85, 0.2)',
    border: `1px solid ${dracula.red}`,
    borderRadius: '6px',
    color: dracula.red,
    fontSize: '13px',
    maxWidth: '400px',
  },
  loading: {
    color: dracula.comment,
    fontSize: '14px',
  },
};

export default function PlanSelector() {
  const [isDragging, setIsDragging] = useState(false);
  const [localError, setLocalError] = useState<string | null>(null);

  const planIndex = useStore((s) => s.planIndex);
  const indexLoading = useStore((s) => s.indexLoading);
  const indexError = useStore((s) => s.indexError);
  const planLoading = useStore((s) => s.planLoading);
  const planError = useStore((s) => s.planError);
  const loadIndex = useStore((s) => s.loadIndex);
  const loadPlanByName = useStore((s) => s.loadPlanByName);
  const loadPlan = useStore((s) => s.loadPlan);

  // Load index on mount
  useEffect(() => {
    loadIndex();
  }, [loadIndex]);

  const handleFile = useCallback(
    async (file: File) => {
      setLocalError(null);

      if (!file.name.endsWith('.json')) {
        setLocalError('Please drop a .json file');
        return;
      }

      try {
        const text = await file.text();
        const json = JSON.parse(text) as unknown;

        if (
          typeof json !== 'object' ||
          json === null ||
          !('schema_version' in json) ||
          !('plan_name' in json) ||
          !('nodes' in json)
        ) {
          setLocalError('Invalid plan JSON: missing required fields');
          return;
        }

        const plan = json as PlanJson;
        if (plan.schema_version !== 1) {
          setLocalError(`Unsupported schema_version: ${plan.schema_version}`);
          return;
        }

        loadPlan(plan, file.name);
      } catch (e) {
        setLocalError(`Failed to parse JSON: ${e instanceof Error ? e.message : String(e)}`);
      }
    },
    [loadPlan]
  );

  const handleDrop = useCallback(
    (e: React.DragEvent) => {
      e.preventDefault();
      setIsDragging(false);
      const file = e.dataTransfer.files[0];
      if (file) handleFile(file);
    },
    [handleFile]
  );

  const handleDragOver = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(true);
  }, []);

  const handleDragLeave = useCallback((e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
  }, []);

  const handleBrowse = useCallback(() => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = (e) => {
      const file = (e.target as HTMLInputElement).files?.[0];
      if (file) handleFile(file);
    };
    input.click();
  }, [handleFile]);

  const error = localError || planError || indexError;

  return (
    <div style={styles.container}>
      <div style={styles.title}>Plan Visualizer</div>
      <div style={styles.subtitle}>Select a plan to visualize</div>

      {indexLoading ? (
        <div style={styles.loading}>Loading plans...</div>
      ) : planIndex && planIndex.length > 0 ? (
        <div style={styles.planList}>
          {planIndex.map((plan) => (
            <button
              key={plan.name}
              style={styles.planButton}
              onClick={() => loadPlanByName(plan.name)}
              disabled={planLoading}
              onMouseOver={(e) => {
                e.currentTarget.style.borderColor = dracula.purple;
                e.currentTarget.style.background = dracula.selected;
              }}
              onMouseOut={(e) => {
                e.currentTarget.style.borderColor = dracula.border;
                e.currentTarget.style.background = dracula.currentLine;
              }}
            >
              <span style={styles.planName}>{plan.name}</span>
              <span style={styles.planMeta}>{plan.built_by?.backend || 'unknown'}</span>
            </button>
          ))}
        </div>
      ) : null}

      <div style={styles.divider}>
        <div style={styles.dividerLine} />
        <span style={styles.dividerText}>or drop a file</span>
        <div style={styles.dividerLine} />
      </div>

      <div
        style={{
          ...styles.dropZone,
          ...(isDragging ? styles.dropZoneActive : {}),
        }}
        onDrop={handleDrop}
        onDragOver={handleDragOver}
        onDragLeave={handleDragLeave}
        onClick={handleBrowse}
      >
        <span style={styles.dropText}>Drop plan.json or click to browse</span>
      </div>

      {error && <div style={styles.error}>{error}</div>}
    </div>
  );
}
