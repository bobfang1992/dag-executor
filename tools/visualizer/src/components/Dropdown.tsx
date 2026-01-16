import { useState, useRef, useEffect } from 'react';
import { dracula } from '../theme';

const styles: Record<string, React.CSSProperties> = {
  container: {
    position: 'relative',
    minWidth: '160px',
  },
  trigger: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    gap: '8px',
    padding: '5px 10px',
    background: dracula.background,
    border: `1px solid ${dracula.border}`,
    borderRadius: '4px',
    color: dracula.foreground,
    fontSize: '12px',
    cursor: 'pointer',
    width: '100%',
    textAlign: 'left',
    transition: 'border-color 0.2s',
  },
  triggerOpen: {
    borderColor: dracula.purple,
  },
  arrow: {
    fontSize: '10px',
    color: dracula.comment,
    transition: 'transform 0.2s',
  },
  arrowOpen: {
    transform: 'rotate(180deg)',
  },
  menu: {
    position: 'absolute',
    top: 'calc(100% + 4px)',
    left: 0,
    right: 0,
    background: dracula.headerBg,
    border: `1px solid ${dracula.border}`,
    borderRadius: '4px',
    boxShadow: '0 4px 12px rgba(0, 0, 0, 0.3)',
    zIndex: 100,
    maxHeight: '200px',
    overflowY: 'auto',
  },
  option: {
    display: 'flex',
    alignItems: 'center',
    gap: '8px',
    padding: '8px 12px',
    fontSize: '12px',
    color: dracula.foreground,
    cursor: 'pointer',
    transition: 'background 0.15s',
  },
  optionSelected: {
    background: dracula.selected,
  },
  optionHover: {
    background: dracula.hover,
  },
  checkmark: {
    width: '14px',
    color: dracula.purple,
    fontSize: '12px',
  },
  label: {
    flex: 1,
    overflow: 'hidden',
    textOverflow: 'ellipsis',
    whiteSpace: 'nowrap',
  },
};

interface DropdownOption {
  value: string;
  label: string;
}

interface DropdownProps {
  options: DropdownOption[];
  value: string;
  onChange: (value: string) => void;
  placeholder?: string;
}

export default function Dropdown({ options, value, onChange, placeholder = 'Select...' }: DropdownProps) {
  const [isOpen, setIsOpen] = useState(false);
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  const selectedOption = options.find(o => o.value === value);

  // Close on click outside
  useEffect(() => {
    const handleClickOutside = (e: MouseEvent) => {
      if (containerRef.current && !containerRef.current.contains(e.target as Node)) {
        setIsOpen(false);
      }
    };

    if (isOpen) {
      document.addEventListener('mousedown', handleClickOutside);
      return () => document.removeEventListener('mousedown', handleClickOutside);
    }
  }, [isOpen]);

  // Keyboard navigation
  useEffect(() => {
    if (!isOpen) return;

    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape') {
        setIsOpen(false);
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        setHoveredIndex(prev =>
          prev === null ? 0 : Math.min(prev + 1, options.length - 1)
        );
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        setHoveredIndex(prev =>
          prev === null ? options.length - 1 : Math.max(prev - 1, 0)
        );
      } else if (e.key === 'Enter' && hoveredIndex !== null) {
        onChange(options[hoveredIndex].value);
        setIsOpen(false);
      }
    };

    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [isOpen, hoveredIndex, options, onChange]);

  const handleSelect = (optionValue: string) => {
    onChange(optionValue);
    setIsOpen(false);
  };

  return (
    <div ref={containerRef} style={styles.container}>
      <button
        type="button"
        style={{
          ...styles.trigger,
          ...(isOpen ? styles.triggerOpen : {}),
        }}
        onClick={() => setIsOpen(!isOpen)}
      >
        <span style={styles.label}>
          {selectedOption?.label ?? placeholder}
        </span>
        <span style={{
          ...styles.arrow,
          ...(isOpen ? styles.arrowOpen : {}),
        }}>
          ▼
        </span>
      </button>

      {isOpen && (
        <div style={styles.menu}>
          {options.map((option, index) => {
            const isSelected = option.value === value;
            const isHovered = index === hoveredIndex;

            return (
              <div
                key={option.value}
                style={{
                  ...styles.option,
                  ...(isSelected ? styles.optionSelected : {}),
                  ...(isHovered && !isSelected ? styles.optionHover : {}),
                }}
                onClick={() => handleSelect(option.value)}
                onMouseEnter={() => setHoveredIndex(index)}
                onMouseLeave={() => setHoveredIndex(null)}
              >
                <span style={styles.checkmark}>
                  {isSelected ? '✓' : ''}
                </span>
                <span style={styles.label}>{option.label}</span>
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
