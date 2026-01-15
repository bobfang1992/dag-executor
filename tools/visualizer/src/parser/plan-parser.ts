import type { PlanJson, VisNode, VisEdge, VisGraph } from '../types';

const NODE_MIN_WIDTH = 100;
const NODE_PADDING = 24;
const NODE_HEIGHT = 50;

// Approximate text width (rough estimate, will be refined in Canvas)
function estimateTextWidth(text: string, fontSize: number): number {
  return text.length * fontSize * 0.6;
}

export function parsePlan(plan: PlanJson): VisGraph {
  const outputSet = new Set(plan.outputs);
  const nodes = new Map<string, VisNode>();
  const edges: VisEdge[] = [];

  // Parse nodes
  for (const node of plan.nodes) {
    const trace = typeof node.params.trace === 'string' ? node.params.trace : null;
    // Show op as main label, trace is secondary
    const label = node.op;

    // Calculate width based on text
    const opWidth = estimateTextWidth(node.op, 11);
    const traceWidth = trace ? estimateTextWidth(`trace: ${trace}`, 9) : 0;
    const textWidth = Math.max(opWidth, traceWidth);
    const nodeWidth = Math.max(NODE_MIN_WIDTH, textWidth + NODE_PADDING);

    const visNode: VisNode = {
      id: node.node_id,
      op: node.op,
      label,
      trace,
      isSource: node.inputs.length === 0,
      isOutput: outputSet.has(node.node_id),
      x: 0,
      y: 0,
      width: nodeWidth,
      height: NODE_HEIGHT,
      params: node.params,
      // Fragment fields (future)
      fragment: undefined,
      fragmentVersion: undefined,
      isFragment: false,
      children: undefined,
      expanded: true,
    };

    nodes.set(node.node_id, visNode);

    // Build edges from inputs
    for (const inputId of node.inputs) {
      edges.push({
        id: `${inputId}->${node.node_id}`,
        from: inputId,
        to: node.node_id,
        hidden: false,
      });
    }
  }

  return { nodes, edges };
}

import { draculaHex } from '../theme';

export function getNodeColor(op: string, isOutput: boolean): number {
  if (isOutput) return draculaHex.nodeOutput;      // red
  if (op.startsWith('viewer.')) return draculaHex.nodeSource;  // cyan
  if (op === 'concat') return draculaHex.nodeCompose;  // pink
  if (op === 'vm') return draculaHex.nodeTransform;   // purple
  if (op === 'filter') return draculaHex.orange;      // orange for filter
  if (op === 'take') return draculaHex.nodeTake;      // green
  return draculaHex.nodeDefault;  // gray
}
