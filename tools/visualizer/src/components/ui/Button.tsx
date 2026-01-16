import { ButtonHTMLAttributes, forwardRef } from 'react';

type ButtonVariant = 'primary' | 'secondary' | 'danger' | 'ghost';
type ButtonSize = 'sm' | 'md' | 'lg';

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  size?: ButtonSize;
}

const baseStyles: React.CSSProperties = {
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
  borderRadius: '8px',
  cursor: 'pointer',
  fontWeight: 500,
  transition: 'all 0.15s ease',
  fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
  border: 'none',
  outline: 'none',
};

const sizeStyles: Record<ButtonSize, React.CSSProperties> = {
  sm: {
    padding: '6px 12px',
    fontSize: '12px',
  },
  md: {
    padding: '10px 20px',
    fontSize: '14px',
  },
  lg: {
    padding: '12px 24px',
    fontSize: '16px',
  },
};

const variantStyles: Record<ButtonVariant, {
  default: React.CSSProperties;
  hover: React.CSSProperties;
  disabled: React.CSSProperties;
}> = {
  primary: {
    default: {
      background: '#0092ff',
      color: '#fff',
    },
    hover: {
      background: '#0080e0',
    },
    disabled: {
      background: '#ccc',
      cursor: 'not-allowed',
    },
  },
  secondary: {
    default: {
      background: '#f5f5f5',
      color: '#333',
      border: '1px solid #e0e0e0',
    },
    hover: {
      background: '#eee',
    },
    disabled: {
      background: '#f5f5f5',
      color: '#999',
      cursor: 'not-allowed',
    },
  },
  danger: {
    default: {
      background: '#dc3545',
      color: '#fff',
    },
    hover: {
      background: '#c82333',
    },
    disabled: {
      background: '#e99',
      cursor: 'not-allowed',
    },
  },
  ghost: {
    default: {
      background: 'transparent',
      color: '#666',
    },
    hover: {
      background: '#f5f5f5',
      color: '#333',
    },
    disabled: {
      color: '#ccc',
      cursor: 'not-allowed',
    },
  },
};

const Button = forwardRef<HTMLButtonElement, ButtonProps>(
  ({ variant = 'primary', size = 'md', disabled, style, onMouseEnter, onMouseLeave, ...props }, ref) => {
    const variantStyle = variantStyles[variant];

    return (
      <button
        ref={ref}
        disabled={disabled}
        style={{
          ...baseStyles,
          ...sizeStyles[size],
          ...variantStyle.default,
          ...(disabled ? variantStyle.disabled : {}),
          ...style,
        }}
        onMouseEnter={(e) => {
          if (!disabled) {
            Object.assign(e.currentTarget.style, variantStyle.hover);
          }
          onMouseEnter?.(e);
        }}
        onMouseLeave={(e) => {
          if (!disabled) {
            Object.assign(e.currentTarget.style, variantStyle.default);
          }
          onMouseLeave?.(e);
        }}
        {...props}
      />
    );
  }
);

Button.displayName = 'Button';

export default Button;
export type { ButtonProps, ButtonVariant, ButtonSize };
