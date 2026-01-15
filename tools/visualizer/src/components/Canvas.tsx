import { useRef, useEffect, useCallback, useState } from 'react';
import { Application, Container, Graphics, Text, TextStyle } from 'pixi.js';
import { useStore } from '../state/store';
import { getNodeColor } from '../parser/plan-parser';
import { draculaHex } from '../theme';

const NODE_CORNER_RADIUS = 6;
const EDGE_COLOR = draculaHex.edgeDefault;
const EDGE_HOVER_COLOR = draculaHex.edgeHighlight;
const EDGE_WIDTH = 2;
const ARROW_SIZE = 8;

export default function Canvas() {
  const containerRef = useRef<HTMLDivElement>(null);
  const appRef = useRef<Application | null>(null);
  const graphContainerRef = useRef<Container | null>(null);
  const isDraggingRef = useRef(false);
  const lastPosRef = useRef({ x: 0, y: 0 });
  const [isReady, setIsReady] = useState(false);

  const graph = useStore((s) => s.graph);
  const view = useStore((s) => s.view);
  const setView = useStore((s) => s.setView);
  const selectedNodeId = useStore((s) => s.selectedNodeId);
  const hoveredNodeId = useStore((s) => s.hoveredNodeId);
  const selectNode = useStore((s) => s.selectNode);
  const hoverNode = useStore((s) => s.hoverNode);
  const fitToView = useStore((s) => s.fitToView);
  const showSource = useStore((s) => s.showSource);

  // Initialize PixiJS
  useEffect(() => {
    if (!containerRef.current) return;

    let destroyed = false;
    const app = new Application();

    const initApp = async () => {
      try {
        await app.init({
          background: draculaHex.background,
          resizeTo: containerRef.current!,
          antialias: true,
          resolution: window.devicePixelRatio || 1,
          autoDensity: true,
        });

        if (destroyed) {
          app.destroy(true, { children: true });
          return;
        }

        containerRef.current!.appendChild(app.canvas);
        appRef.current = app;

        const graphContainer = new Container();
        graphContainerRef.current = graphContainer;
        app.stage.addChild(graphContainer);

        setIsReady(true);
      } catch (e) {
        console.error('PixiJS init failed:', e);
      }
    };

    initApp();

    return () => {
      destroyed = true;
      setIsReady(false);
      if (appRef.current) {
        appRef.current.destroy(true, { children: true });
        appRef.current = null;
      }
    };
  }, []);

  // Fit to view once ready and we have a graph
  useEffect(() => {
    if (!isReady || !graph || !containerRef.current) return;
    fitToView(containerRef.current.clientWidth, containerRef.current.clientHeight);
  }, [isReady, graph, fitToView]);

  // Resize canvas when container size changes (e.g., source panel opens/closes)
  useEffect(() => {
    if (!containerRef.current || !isReady) return;
    const container = containerRef.current;

    const resizeObserver = new ResizeObserver((entries) => {
      const entry = entries[0];
      if (!entry) return;

      const { width, height } = entry.contentRect;
      if (width === 0 || height === 0) return;

      // Resize the canvas
      if (appRef.current) {
        appRef.current.resize();
      }
    });

    resizeObserver.observe(container);
    return () => resizeObserver.disconnect();
  }, [isReady]);

  // Refit when source panel toggles
  useEffect(() => {
    if (!isReady || !graph || !containerRef.current) return;
    // Small delay to let the resize complete
    const timer = setTimeout(() => {
      if (containerRef.current) {
        fitToView(containerRef.current.clientWidth, containerRef.current.clientHeight);
      }
    }, 50);
    return () => clearTimeout(timer);
  }, [showSource, isReady, graph, fitToView]);

  // Update view transform
  useEffect(() => {
    if (!graphContainerRef.current) return;
    graphContainerRef.current.x = view.x;
    graphContainerRef.current.y = view.y;
    graphContainerRef.current.scale.set(view.scale);
  }, [view]);

  // Render graph
  useEffect(() => {
    if (!isReady) return;
    const container = graphContainerRef.current;
    if (!container || !graph) return;

    container.removeChildren();

    // Get connected nodes for hover highlighting
    const connectedNodes = new Set<string>();
    if (hoveredNodeId) {
      connectedNodes.add(hoveredNodeId);
      for (const edge of graph.edges) {
        if (edge.from === hoveredNodeId) connectedNodes.add(edge.to);
        if (edge.to === hoveredNodeId) connectedNodes.add(edge.from);
      }
    }

    // Draw edges first (below nodes)
    const edgesGraphics = new Graphics();
    for (const edge of graph.edges) {
      const fromNode = graph.nodes.get(edge.from);
      const toNode = graph.nodes.get(edge.to);
      if (!fromNode || !toNode) continue;

      const isHighlighted =
        hoveredNodeId && (edge.from === hoveredNodeId || edge.to === hoveredNodeId);
      const color = isHighlighted ? EDGE_HOVER_COLOR : EDGE_COLOR;
      const alpha = hoveredNodeId && !isHighlighted ? 0.2 : 1;

      const x1 = fromNode.x + fromNode.width / 2;
      const y1 = fromNode.y + fromNode.height;
      const x2 = toNode.x + toNode.width / 2;
      const y2 = toNode.y;

      // Draw line
      edgesGraphics.moveTo(x1, y1);
      edgesGraphics.lineTo(x2, y2);
      edgesGraphics.stroke({ width: EDGE_WIDTH, color, alpha });

      // Draw arrow
      const angle = Math.atan2(y2 - y1, x2 - x1);
      const arrowX = x2 - Math.cos(angle) * 2;
      const arrowY = y2 - Math.sin(angle) * 2;

      edgesGraphics.moveTo(arrowX, arrowY);
      edgesGraphics.lineTo(
        arrowX - ARROW_SIZE * Math.cos(angle - Math.PI / 6),
        arrowY - ARROW_SIZE * Math.sin(angle - Math.PI / 6)
      );
      edgesGraphics.lineTo(
        arrowX - ARROW_SIZE * Math.cos(angle + Math.PI / 6),
        arrowY - ARROW_SIZE * Math.sin(angle + Math.PI / 6)
      );
      edgesGraphics.lineTo(arrowX, arrowY);
      edgesGraphics.fill({ color, alpha });
    }
    container.addChild(edgesGraphics);

    // Draw nodes
    for (const [id, node] of graph.nodes) {
      const nodeContainer = new Container();
      nodeContainer.x = node.x;
      nodeContainer.y = node.y;
      nodeContainer.eventMode = 'static';
      nodeContainer.cursor = 'pointer';

      const isSelected = id === selectedNodeId;
      const isDimmed = hoveredNodeId && !connectedNodes.has(id);

      // Node background
      const bg = new Graphics();
      const fillColor = getNodeColor(node.op, node.isOutput);
      const nodeAlpha = isDimmed ? 0.3 : 1;

      bg.roundRect(0, 0, node.width, node.height, NODE_CORNER_RADIUS);
      bg.fill({ color: fillColor, alpha: nodeAlpha });

      
      nodeContainer.addChild(bg);

      // Node label (op name) - use dark text on light node backgrounds
      const labelStyle = new TextStyle({
        fontSize: 12,
        fill: isDimmed ? draculaHex.comment : draculaHex.background,
        fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
        fontWeight: 'bold',
      });
      const label = new Text({ text: node.label, style: labelStyle });

      // If there's a trace, show it smaller below with "trace:" prefix
      if (node.trace) {
        const traceStyle = new TextStyle({
          fontSize: 10,
          fill: isDimmed ? draculaHex.comment : draculaHex.currentLine,
          fontFamily: 'ui-monospace, SFMono-Regular, Menlo, monospace',
        });
        const traceText = new Text({ text: `trace: ${node.trace}`, style: traceStyle });

        // Stack label and trace vertically
        const totalHeight = label.height + traceText.height + 2;
        label.x = (node.width - label.width) / 2;
        label.y = (node.height - totalHeight) / 2;
        traceText.x = (node.width - traceText.width) / 2;
        traceText.y = label.y + label.height + 2;

        nodeContainer.addChild(label);
        nodeContainer.addChild(traceText);
      } else {
        label.x = (node.width - label.width) / 2;
        label.y = (node.height - label.height) / 2;
        nodeContainer.addChild(label);
      }

      // Selection highlight
      if (isSelected) {
        const highlight = new Graphics();
        highlight.roundRect(-3, -3, node.width + 6, node.height + 6, NODE_CORNER_RADIUS + 2);
        highlight.stroke({ width: 2, color: draculaHex.yellow });
        nodeContainer.addChildAt(highlight, 0);
      }

      // Interaction
      nodeContainer.on('pointerdown', (e) => {
        e.stopPropagation();
        selectNode(id);
      });
      nodeContainer.on('pointerenter', () => hoverNode(id));
      nodeContainer.on('pointerleave', () => hoverNode(null));

      container.addChild(nodeContainer);
    }
  }, [isReady, graph, selectedNodeId, hoveredNodeId, selectNode, hoverNode]);

  // Pan handling
  const handlePointerDown = useCallback((e: React.PointerEvent) => {
    if (e.target === containerRef.current?.querySelector('canvas')) {
      isDraggingRef.current = true;
      lastPosRef.current = { x: e.clientX, y: e.clientY };
      selectNode(null);
    }
  }, [selectNode]);

  const handlePointerMove = useCallback(
    (e: React.PointerEvent) => {
      if (!isDraggingRef.current) return;

      const dx = e.clientX - lastPosRef.current.x;
      const dy = e.clientY - lastPosRef.current.y;
      lastPosRef.current = { x: e.clientX, y: e.clientY };

      setView({ x: view.x + dx, y: view.y + dy });
    },
    [view.x, view.y, setView]
  );

  const handlePointerUp = useCallback(() => {
    isDraggingRef.current = false;
  }, []);

  // Zoom handling
  const handleWheel = useCallback(
    (e: React.WheelEvent) => {
      e.preventDefault();

      const rect = containerRef.current?.getBoundingClientRect();
      if (!rect) return;

      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top;

      const zoomFactor = e.deltaY > 0 ? 0.9 : 1.1;
      const newScale = Math.max(0.1, Math.min(4, view.scale * zoomFactor));

      // Zoom toward mouse position
      const scaleRatio = newScale / view.scale;
      const newX = mouseX - (mouseX - view.x) * scaleRatio;
      const newY = mouseY - (mouseY - view.y) * scaleRatio;

      setView({ x: newX, y: newY, scale: newScale });
    },
    [view, setView]
  );

  return (
    <div
      ref={containerRef}
      style={{ width: '100%', height: '100%', overflow: 'hidden' }}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handlePointerUp}
      onPointerLeave={handlePointerUp}
      onWheel={handleWheel}
    />
  );
}
