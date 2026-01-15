import dagre from 'dagre';
import type { VisGraph, VisNode } from '../types';

interface LayoutOptions {
  rankdir?: 'TB' | 'BT' | 'LR' | 'RL';
  ranksep?: number;
  nodesep?: number;
  edgesep?: number;
}

export function layoutGraph(
  graph: VisGraph,
  options: LayoutOptions = {}
): VisGraph {
  const { rankdir = 'TB', ranksep = 60, nodesep = 30, edgesep = 20 } = options;

  const g = new dagre.graphlib.Graph();
  g.setGraph({ rankdir, ranksep, nodesep, edgesep });
  g.setDefaultEdgeLabel(() => ({}));

  // Add nodes
  for (const [id, node] of graph.nodes) {
    g.setNode(id, { width: node.width, height: node.height });
  }

  // Add edges
  for (const edge of graph.edges) {
    g.setEdge(edge.from, edge.to);
  }

  // Run layout
  dagre.layout(g);

  // Update node positions
  const newNodes = new Map<string, VisNode>();
  for (const [id, node] of graph.nodes) {
    const layoutNode = g.node(id);
    if (layoutNode) {
      newNodes.set(id, {
        ...node,
        // dagre gives center coords, convert to top-left
        x: layoutNode.x - node.width / 2,
        y: layoutNode.y - node.height / 2,
      });
    } else {
      newNodes.set(id, node);
    }
  }

  return { nodes: newNodes, edges: graph.edges };
}
