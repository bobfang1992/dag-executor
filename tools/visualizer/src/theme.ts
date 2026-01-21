// US Graphics inspired color palette
// https://usgraphics.com/
// Clean, light theme - white bg, black text, minimal accents

export const dracula = {
  // Base colors (light theme)
  background: '#ffffff',
  currentLine: '#f4f4f4',
  foreground: '#000000',
  comment: '#666666',

  // Accent colors (used sparingly)
  cyan: '#0092ff',
  green: '#00c986',
  orange: '#ffb700',
  pink: '#ff41b4',
  purple: '#0092ff',  // use blue as primary accent
  red: '#ff0037',
  yellow: '#ffb700',

  // Semantic mappings for our app
  headerBg: '#ffffff',
  panelBg: '#ffffff',
  border: '#e0e0e0',
  textPrimary: '#000000',
  textSecondary: '#666666',
  textMuted: '#999999',

  // Node colors by type (saturated for visibility on white)
  nodeSource: '#0092ff',     // blue - sources
  nodeTransform: '#8969ff',  // purple - vm
  nodeCompose: '#ff41b4',    // pink - concat
  nodeOutput: '#ff0037',     // red - output
  nodeTake: '#00c986',       // green - take
  nodeDefault: '#666666',    // gray

  // Interactive
  hover: '#f4f4f4',
  selected: '#e8e8e8',
  buttonBg: '#f4f4f4',
  buttonHover: '#e0e0e0',

  // Status badges
  statusActive: '#00c986',
  statusActiveText: '#ffffff',
  statusImplemented: '#00c986',
  statusImplementedText: '#ffffff',
  statusDeprecated: '#ffb700',
  statusDeprecatedText: '#000000',
  statusDraft: '#0092ff',
  statusDraftText: '#ffffff',
  statusBlocked: '#ff0037',
  statusBlockedText: '#ffffff',
};

// PixiJS uses hex numbers
export const draculaHex = {
  background: 0xffffff,
  currentLine: 0xf4f4f4,
  foreground: 0x000000,
  comment: 0x666666,
  cyan: 0x0092ff,
  green: 0x00c986,
  orange: 0xffb700,
  pink: 0xff41b4,
  purple: 0x0092ff,
  red: 0xff0037,
  yellow: 0xffb700,

  nodeSource: 0x0092ff,
  nodeTransform: 0x8969ff,
  nodeCompose: 0xff41b4,
  nodeOutput: 0xff0037,
  nodeTake: 0x00c986,
  nodeDefault: 0x666666,

  edgeDefault: 0xcccccc,
  edgeHighlight: 0x0092ff,
};
