import { useState, useRef, useEffect } from 'react';
import { dracula } from '../theme';

interface MenuItem {
  label: string;
  onClick: () => void;
  disabled?: boolean;
}

interface MenuProps {
  label: string;
  items: MenuItem[];
}

function Menu({ label, items }: MenuProps) {
  const [open, setOpen] = useState(false);
  const menuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    function handleClickOutside(e: MouseEvent) {
      if (menuRef.current && !menuRef.current.contains(e.target as Node)) {
        setOpen(false);
      }
    }
    if (open) {
      document.addEventListener('mousedown', handleClickOutside);
      return () => document.removeEventListener('mousedown', handleClickOutside);
    }
  }, [open]);

  return (
    <div ref={menuRef} style={{ position: 'relative' }}>
      <button
        onClick={() => setOpen(!open)}
        style={{
          padding: '4px 12px',
          background: open ? dracula.currentLine : 'transparent',
          border: 'none',
          borderRadius: '4px',
          color: dracula.foreground,
          fontSize: '13px',
          cursor: 'pointer',
          transition: 'background 0.15s',
        }}
        onMouseEnter={(e) => {
          if (!open) e.currentTarget.style.background = dracula.currentLine;
        }}
        onMouseLeave={(e) => {
          if (!open) e.currentTarget.style.background = 'transparent';
        }}
      >
        {label}
      </button>
      {open && (
        <div
          style={{
            position: 'absolute',
            top: '100%',
            left: 0,
            marginTop: '4px',
            background: dracula.background,
            border: `1px solid ${dracula.border}`,
            borderRadius: '6px',
            boxShadow: '0 4px 12px rgba(0,0,0,0.15)',
            minWidth: '160px',
            zIndex: 1000,
            overflow: 'hidden',
          }}
        >
          {items.map((item, i) => (
            <button
              key={i}
              onClick={() => {
                if (!item.disabled) {
                  item.onClick();
                  setOpen(false);
                }
              }}
              disabled={item.disabled}
              style={{
                display: 'block',
                width: '100%',
                padding: '8px 12px',
                background: 'transparent',
                border: 'none',
                textAlign: 'left',
                color: item.disabled ? dracula.comment : dracula.foreground,
                fontSize: '13px',
                cursor: item.disabled ? 'default' : 'pointer',
                transition: 'background 0.15s',
              }}
              onMouseEnter={(e) => {
                if (!item.disabled) e.currentTarget.style.background = dracula.currentLine;
              }}
              onMouseLeave={(e) => {
                e.currentTarget.style.background = 'transparent';
              }}
            >
              {item.label}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

interface MenuBarProps {
  onAddPanel: (panel: 'editor' | 'canvas' | 'details' | 'source') => void;
  onResetLayout: () => void;
  existingPanels: Set<string>;
  mode: 'view' | 'edit';
}

export default function MenuBar({ onAddPanel, onResetLayout, existingPanels, mode }: MenuBarProps) {
  const addItems: MenuItem[] = [
    {
      label: 'Editor',
      onClick: () => onAddPanel('editor'),
      disabled: existingPanels.has('editor') || mode === 'view',
    },
    {
      label: 'Canvas',
      onClick: () => onAddPanel('canvas'),
      disabled: existingPanels.has('canvas'),
    },
    {
      label: 'Details',
      onClick: () => onAddPanel('details'),
      disabled: existingPanels.has('details'),
    },
    {
      label: 'Source',
      onClick: () => onAddPanel('source'),
      disabled: existingPanels.has('source') || mode === 'edit',
    },
  ];

  const viewItems: MenuItem[] = [
    {
      label: 'Reset Layout',
      onClick: onResetLayout,
    },
  ];

  return (
    <div style={{ display: 'flex', gap: '4px', marginLeft: '16px' }}>
      <Menu label="Add" items={addItems} />
      <Menu label="View" items={viewItems} />
    </div>
  );
}
