import { useState, useCallback, useEffect, useRef } from 'react';
import { Button } from './ui';

// Primary accent color (matches Button)
const PRIMARY_COLOR = '#0092ff';

const styles: Record<string, React.CSSProperties> = {
  overlay: {
    position: 'fixed',
    top: 0,
    left: 0,
    right: 0,
    bottom: 0,
    background: 'rgba(0, 0, 0, 0.5)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    zIndex: 1000,
    backdropFilter: 'blur(4px)',
  },
  modal: {
    background: '#fff',
    borderRadius: '12px',
    border: 'none',
    padding: '24px',
    minWidth: '320px',
    maxWidth: '420px',
    boxShadow: '0 20px 60px rgba(0, 0, 0, 0.3)',
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
  },
  title: {
    fontSize: '18px',
    fontWeight: 600,
    color: '#1a1a1a',
    marginBottom: '12px',
  },
  message: {
    fontSize: '14px',
    color: '#666',
    marginBottom: '20px',
    lineHeight: 1.6,
  },
  input: {
    width: '100%',
    padding: '12px 14px',
    fontSize: '14px',
    background: '#f8f8f8',
    border: '1px solid #e0e0e0',
    borderRadius: '8px',
    color: '#1a1a1a',
    marginBottom: '20px',
    outline: 'none',
    boxSizing: 'border-box',
    fontFamily: 'inherit',
  },
  buttons: {
    display: 'flex',
    justifyContent: 'flex-end',
    gap: '10px',
  },
};

interface PromptModalProps {
  title: string;
  message?: string;
  defaultValue?: string;
  placeholder?: string;
  onConfirm: (value: string) => void;
  onCancel: () => void;
}

export function PromptModal({
  title,
  message,
  defaultValue = '',
  placeholder,
  onConfirm,
  onCancel,
}: PromptModalProps) {
  const [value, setValue] = useState(defaultValue);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    inputRef.current?.focus();
    inputRef.current?.select();
  }, []);

  const handleKeyDown = useCallback((e: React.KeyboardEvent) => {
    if (e.key === 'Enter' && value.trim()) {
      onConfirm(value.trim());
    } else if (e.key === 'Escape') {
      onCancel();
    }
  }, [value, onConfirm, onCancel]);

  return (
    <div style={styles.overlay} onClick={onCancel}>
      <div style={styles.modal} onClick={(e) => e.stopPropagation()}>
        <div style={styles.title}>{title}</div>
        {message && <div style={styles.message}>{message}</div>}
        <input
          ref={inputRef}
          type="text"
          style={styles.input}
          value={value}
          onChange={(e) => setValue(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={placeholder}
          onFocus={(e) => {
            e.currentTarget.style.borderColor = PRIMARY_COLOR;
            e.currentTarget.style.background = '#fff';
          }}
          onBlur={(e) => {
            e.currentTarget.style.borderColor = '#e0e0e0';
            e.currentTarget.style.background = '#f8f8f8';
          }}
        />
        <div style={styles.buttons}>
          <Button variant="secondary" onClick={onCancel}>
            Cancel
          </Button>
          <Button
            variant="primary"
            onClick={() => value.trim() && onConfirm(value.trim())}
            disabled={!value.trim()}
          >
            Save
          </Button>
        </div>
      </div>
    </div>
  );
}

interface ConfirmModalProps {
  title: string;
  message: string;
  confirmLabel?: string;
  danger?: boolean;
  onConfirm: () => void;
  onCancel: () => void;
}

export function ConfirmModal({
  title,
  message,
  confirmLabel = 'Confirm',
  danger = false,
  onConfirm,
  onCancel,
}: ConfirmModalProps) {
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onCancel();
      if (e.key === 'Enter') onConfirm();
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [onConfirm, onCancel]);

  return (
    <div style={styles.overlay} onClick={onCancel}>
      <div style={styles.modal} onClick={(e) => e.stopPropagation()}>
        <div style={styles.title}>{title}</div>
        <div style={styles.message}>{message}</div>
        <div style={styles.buttons}>
          <Button variant="secondary" onClick={onCancel}>
            Cancel
          </Button>
          <Button variant={danger ? 'danger' : 'primary'} onClick={onConfirm}>
            {confirmLabel}
          </Button>
        </div>
      </div>
    </div>
  );
}
