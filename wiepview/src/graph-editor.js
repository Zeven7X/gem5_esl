import { descendantsOf } from "./model.js";

const SVG_NS = "http://www.w3.org/2000/svg";
const MIN_NODE_WIDTH = 170;
const MIN_NODE_HEIGHT = 110;

export class GraphEditor {
  constructor(host, architecture, view, callbacks = {}) {
    this.host = host;
    this.architecture = architecture;
    this.view = view;
    this.callbacks = callbacks;
    this.selection = null;
    this.interaction = null;

    this.svg = svgElement("svg", { role: "img", "aria-label": "WiepView architecture" });
    this.world = svgElement("g", { class: "graph-world" });
    this.svg.append(this.world);
    this.host.replaceChildren(this.svg);

    this.bindEvents();
    this.render();
  }

  replaceDocument(architecture, view) {
    this.architecture = architecture;
    this.view = view;
    this.selection = null;
    this.render();
  }

  render() {
    this.world.replaceChildren();
    this.applyViewport();

    const containerLayer = svgElement("g", { class: "container-layer" });
    const edgeLayer = svgElement("g", { class: "edge-layer" });
    const objectLayer = svgElement("g", { class: "object-layer" });
    const controlLayer = svgElement("g", { class: "control-layer" });
    this.world.append(containerLayer, edgeLayer, objectLayer, controlLayer);

    for (const connection of this.architecture.connections) {
      const edge = this.renderEdge(connection);
      if (edge) {
        edgeLayer.append(...edge);
      }
    }

    for (const node of this.architecture.nodes) {
      if (!this.isNodeHidden(node.id)) {
        const layer = node.kind === "simobj" ? objectLayer : containerLayer;
        layer.append(this.renderNode(node));
        if (node.kind === "cluster") {
          controlLayer.append(this.renderCollapseControl(node));
        }
        controlLayer.append(this.renderPortControls(node));
      }
    }
  }

  renderNode(node) {
    const style = this.view.nodes[node.id];
    const selected = this.selection?.type === "node" && this.selection.id === node.id;
    const group = svgElement("g", {
      class: `graph-node ${selected ? "selected" : ""}`,
      transform: `translate(${style.x} ${style.y})`,
      "data-node-id": node.id,
    });

    group.append(this.createNodeSurface(style));
    group.append(
      svgElement("line", {
        class: "node-header-line",
        x1: 0,
        y1: 67,
        x2: style.width,
        y2: 67,
      }),
    );

    group.append(
      textElement(node.kind === "simobj" ? "OBJECT" : node.kind.toUpperCase(), 18, 21, {
        class: "node-kind-label",
      }),
      textElement(trimLabel(style.label, 32), 18, 47, { class: "node-title" }),
      textElement(trimLabel(style.type_label ?? node.className, 34), 18, 87, {
        class: "node-class",
      }),
      textElement(trimMiddle(style.variable_label ?? node.id, 43), 18, 105, {
        class: "node-path",
      }),
    );

    this.renderPorts(group, node, style);

    group.append(
      svgElement("circle", {
        class: "resize-handle",
        cx: style.width,
        cy: style.height,
        r: 7,
        "data-resize-id": node.id,
      }),
    );
    return group;
  }

  renderCollapseControl(node) {
    const style = this.view.nodes[node.id];
    const collapse = svgElement("g", {
      "data-collapse-id": node.id,
      transform: `translate(${style.x + style.width - 35} ${style.y + 18})`,
    });
    collapse.addEventListener("pointerdown", (event) => event.stopPropagation());
    collapse.addEventListener("click", (event) => {
      event.stopPropagation();
      this.toggleCollapse(node.id);
    });
    collapse.append(
      svgElement("circle", { class: "collapse-button", r: 12 }),
      textElement(style.collapsed ? "+" : "−", 0, 4, {
        class: "collapse-icon",
        "text-anchor": "middle",
      }),
    );
    return collapse;
  }

  createNodeSurface(style) {
    if (style.shape === "diamond") {
      return svgElement("polygon", {
        class: "node-surface",
        points: `${style.width / 2},0 ${style.width},${style.height / 2} ` +
          `${style.width / 2},${style.height} 0,${style.height / 2}`,
        fill: style.fill,
        stroke: style.border,
      });
    }

    const radius = style.shape === "capsule"
      ? Math.min(style.height / 2, 80)
      : style.shape === "rounded"
        ? 18
        : 3;
    return svgElement("rect", {
      class: "node-surface",
      width: style.width,
      height: style.height,
      rx: radius,
      fill: style.fill,
      stroke: style.border,
    });
  }

  renderPorts(group, node, style) {
    const sides = {
      slv: node.ports.filter((port) => port.direction === "slv"),
      mst: node.ports.filter((port) => port.direction === "mst"),
    };
    for (const direction of ["slv", "mst"]) {
      sides[direction].forEach((port, index) => {
        const portStyle = this.view.ports[port.id];
        const point = localPortPoint(
          style,
          direction,
          index,
          sides[direction].length,
          portStyle,
        );
        const labelPosition = portLabelPoint(point);
        group.append(
          svgElement("circle", {
            class: `port-dot ${direction}`,
            cx: point.x,
            cy: point.y,
            r: 6,
            "data-port-id": port.id,
          }),
          textElement(
            trimLabel(portStyle?.label ?? port.name, 18),
            labelPosition.x,
            labelPosition.y,
            {
              class: "port-label",
              "text-anchor": labelPosition.anchor,
            },
          ),
        );
      });
    }
  }

  renderPortControls(node) {
    const style = this.view.nodes[node.id];
    const group = svgElement("g", {
      class: "port-control-layer",
      transform: `translate(${style.x} ${style.y})`,
    });
    const sides = {
      slv: node.ports.filter((port) => port.direction === "slv"),
      mst: node.ports.filter((port) => port.direction === "mst"),
    };
    for (const direction of ["slv", "mst"]) {
      sides[direction].forEach((port, index) => {
        const point = localPortPoint(
          style,
          direction,
          index,
          sides[direction].length,
          this.view.ports[port.id],
        );
        group.append(svgElement("circle", {
          class: "port-drag-handle",
          cx: point.x,
          cy: point.y,
          r: 12,
          "data-port-id": port.id,
        }));
      });
    }
    return group;
  }

  renderEdge(connection) {
    const fromPort = this.architecture.portById.get(connection.from);
    const toPort = this.architecture.portById.get(connection.to);
    if (!fromPort || !toPort) {
      return null;
    }

    const fromVisibleOwner = this.visibleOwner(fromPort.ownerId);
    const toVisibleOwner = this.visibleOwner(toPort.ownerId);
    const endpointWasProxied =
      fromVisibleOwner !== fromPort.ownerId || toVisibleOwner !== toPort.ownerId;

    // A collapsed cluster absorbs connections wholly contained by its subtree.
    // Boundary-crossing connections keep rendering through the cluster proxy.
    if (endpointWasProxied && fromVisibleOwner === toVisibleOwner) {
      return null;
    }

    const from = this.portPoint(connection.from);
    const to = this.portPoint(connection.to);
    if (!from || !to) {
      return null;
    }

    const style = this.view.connections[connection.id];
    const selected = this.selection?.type === "connection" && this.selection.id === connection.id;
    const distance = Math.max(36, Math.min(180, Math.abs(to.x - from.x) * 0.42));
    const pathData = `M ${from.x} ${from.y} C ${from.x + distance} ${from.y}, ` +
      `${to.x - distance} ${to.y}, ${to.x} ${to.y}`;
    const dash = { dashed: "12 8", dotted: "3 7", solid: "" }[style.line_style] ?? "";

    const hit = svgElement("path", {
      class: "graph-edge-hit",
      d: pathData,
      "data-edge-id": connection.id,
    });
    const visible = svgElement("path", {
      class: `graph-edge ${selected ? "selected" : ""}`,
      d: pathData,
      stroke: selected ? "#d2673a" : style.color,
      "stroke-width": selected ? Number(style.width) + 2 : style.width,
      "stroke-dasharray": dash,
      "stroke-linecap": "round",
      "data-edge-id": connection.id,
    });
    return [hit, visible];
  }

  portPoint(portId) {
    const port = this.architecture.portById.get(portId);
    if (!port) {
      return null;
    }

    const visibleOwnerId = this.visibleOwner(port.ownerId);
    const node = this.architecture.nodeById.get(visibleOwnerId);
    const style = this.view.nodes[visibleOwnerId];
    if (!node || !style) {
      return null;
    }

    if (visibleOwnerId !== port.ownerId) {
      const proxyPort = this.proxyPortFor(port, node);
      if (proxyPort) {
        const sameSide = node.ports.filter((item) => item.direction === proxyPort.direction);
        const index = Math.max(0, sameSide.findIndex((item) => item.id === proxyPort.id));
        const local = localPortPoint(
          style,
          proxyPort.direction,
          index,
          sameSide.length,
          this.view.ports[proxyPort.id],
        );
        return { x: style.x + local.x, y: style.y + local.y };
      }
      return {
        x: style.x + (port.direction === "mst" ? style.width : 0),
        y: style.y + style.height / 2,
      };
    }

    const sameSide = node.ports.filter((item) => item.direction === port.direction);
    const index = Math.max(0, sameSide.findIndex((item) => item.id === portId));
    const local = localPortPoint(
      style,
      port.direction,
      index,
      sameSide.length,
      this.view.ports[portId],
    );
    return { x: style.x + local.x, y: style.y + local.y };
  }

  proxyPortFor(hiddenPort, visibleNode) {
    const candidates = visibleNode.ports.filter(
      (port) => port.direction === hiddenPort.direction,
    );
    const nameMatch = candidates.find((port) => port.name === hiddenPort.name);
    if (nameMatch) {
      return nameMatch;
    }
    return candidates.length === 1 ? candidates[0] : null;
  }

  visibleOwner(nodeId) {
    let current = this.architecture.nodeById.get(nodeId);
    let visible = nodeId;
    while (current?.parentId) {
      const parent = this.architecture.nodeById.get(current.parentId);
      if (parent && this.view.nodes[parent.id]?.collapsed) {
        visible = parent.id;
      }
      current = parent;
    }
    return visible;
  }

  isNodeHidden(nodeId) {
    const node = this.architecture.nodeById.get(nodeId);
    let parentId = node?.parentId;
    while (parentId) {
      if (this.view.nodes[parentId]?.collapsed) {
        return true;
      }
      parentId = this.architecture.nodeById.get(parentId)?.parentId;
    }
    return false;
  }

  select(type, id) {
    this.selection = type && id ? { type, id } : null;
    this.render();
    this.callbacks.onSelectionChange?.(this.selection);
  }

  focusNode(nodeId) {
    const style = this.view.nodes[nodeId];
    if (!style) {
      return;
    }
    const rect = this.host.getBoundingClientRect();
    const zoom = Math.min(1.2, Math.max(0.55, this.view.viewport.zoom));
    this.view.viewport.zoom = zoom;
    this.view.viewport.x = rect.width / 2 - (style.x + style.width / 2) * zoom;
    this.view.viewport.y = rect.height / 2 - (style.y + style.height / 2) * zoom;
    this.select("node", nodeId);
    this.notifyViewChanged(false);
  }

  fitView() {
    const visible = this.architecture.nodes.filter((node) => !this.isNodeHidden(node.id));
    if (!visible.length) {
      return;
    }
    const bounds = visible.reduce(
      (result, node) => {
        const item = this.view.nodes[node.id];
        result.minX = Math.min(result.minX, item.x);
        result.minY = Math.min(result.minY, item.y);
        result.maxX = Math.max(result.maxX, item.x + item.width);
        result.maxY = Math.max(result.maxY, item.y + item.height);
        return result;
      },
      { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity },
    );
    const rect = this.host.getBoundingClientRect();
    const padding = 60;
    const width = Math.max(1, bounds.maxX - bounds.minX);
    const height = Math.max(1, bounds.maxY - bounds.minY);
    const zoom = Math.min(1.3, (rect.width - padding * 2) / width, (rect.height - padding * 2) / height);
    this.view.viewport.zoom = Math.max(0.2, zoom);
    this.view.viewport.x = (rect.width - width * this.view.viewport.zoom) / 2 - bounds.minX * this.view.viewport.zoom;
    this.view.viewport.y = (rect.height - height * this.view.viewport.zoom) / 2 - bounds.minY * this.view.viewport.zoom;
    this.applyViewport();
    this.notifyViewChanged(true);
  }

  setZoom(zoom, center = null) {
    const oldZoom = this.view.viewport.zoom;
    const nextZoom = clamp(zoom, 0.2, 2.5);
    const rect = this.host.getBoundingClientRect();
    const point = center ?? { x: rect.width / 2, y: rect.height / 2 };
    const worldX = (point.x - this.view.viewport.x) / oldZoom;
    const worldY = (point.y - this.view.viewport.y) / oldZoom;
    this.view.viewport.zoom = nextZoom;
    this.view.viewport.x = point.x - worldX * nextZoom;
    this.view.viewport.y = point.y - worldY * nextZoom;
    this.applyViewport();
    this.notifyViewChanged(true);
  }

  applyViewport() {
    const viewport = this.view.viewport;
    this.world.setAttribute(
      "transform",
      `translate(${viewport.x} ${viewport.y}) scale(${viewport.zoom})`,
    );
    this.callbacks.onZoomChange?.(viewport.zoom);
  }

  bindEvents() {
    this.svg.addEventListener("pointerdown", (event) => this.onPointerDown(event));
    this.svg.addEventListener("pointermove", (event) => this.onPointerMove(event));
    this.svg.addEventListener("pointerup", (event) => this.onPointerUp(event));
    this.svg.addEventListener("pointercancel", (event) => this.onPointerUp(event));
    this.svg.addEventListener("dblclick", (event) => {
      const nodeGroup = event.target.closest?.("[data-node-id]");
      if (!nodeGroup) {
        return;
      }
      const node = this.architecture.nodeById.get(nodeGroup.dataset.nodeId);
      if (node?.kind === "cluster") {
        this.toggleCollapse(node.id);
      }
    });
    this.svg.addEventListener(
      "wheel",
      (event) => {
        event.preventDefault();
        const rect = this.host.getBoundingClientRect();
        this.setZoom(this.view.viewport.zoom * (event.deltaY > 0 ? 0.9 : 1.1), {
          x: event.clientX - rect.left,
          y: event.clientY - rect.top,
        });
      },
      { passive: false },
    );
  }

  onPointerDown(event) {
    const portElement = event.target.closest?.("[data-port-id]");
    if (portElement) {
      const portId = portElement.dataset.portId;
      const port = this.architecture.portById.get(portId);
      if (!port) {
        return;
      }
      this.select("node", port.ownerId);
      this.interaction = {
        type: "port",
        id: portId,
        ownerId: port.ownerId,
        pointerId: event.pointerId,
      };
      this.svg.setPointerCapture(event.pointerId);
      return;
    }

    const resize = event.target.closest?.("[data-resize-id]");
    if (resize) {
      const id = resize.dataset.resizeId;
      this.select("node", id);
      this.interaction = {
        type: "resize",
        id,
        pointerId: event.pointerId,
        start: this.screenPoint(event),
        original: { ...this.view.nodes[id] },
      };
      this.svg.setPointerCapture(event.pointerId);
      return;
    }

    const edge = event.target.closest?.("[data-edge-id]");
    if (edge) {
      this.select("connection", edge.dataset.edgeId);
      return;
    }

    const nodeGroup = event.target.closest?.("[data-node-id]");
    if (nodeGroup) {
      const id = nodeGroup.dataset.nodeId;
      const ids = [id, ...descendantsOf(this.architecture, id)];
      const originals = Object.fromEntries(
        ids.map((nodeId) => [nodeId, { x: this.view.nodes[nodeId].x, y: this.view.nodes[nodeId].y }]),
      );
      this.select("node", id);
      this.interaction = {
        type: "drag",
        id,
        ids,
        pointerId: event.pointerId,
        start: this.worldPoint(event),
        originals,
      };
      nodeGroup.classList.add("dragging");
      this.svg.setPointerCapture(event.pointerId);
      return;
    }

    this.select(null, null);
    this.interaction = {
      type: "pan",
      pointerId: event.pointerId,
      start: this.screenPoint(event),
      original: { x: this.view.viewport.x, y: this.view.viewport.y },
    };
    this.svg.setPointerCapture(event.pointerId);
  }

  onPointerMove(event) {
    if (!this.interaction || event.pointerId !== this.interaction.pointerId) {
      return;
    }
    if (this.interaction.type === "pan") {
      const point = this.screenPoint(event);
      this.view.viewport.x = this.interaction.original.x + point.x - this.interaction.start.x;
      this.view.viewport.y = this.interaction.original.y + point.y - this.interaction.start.y;
      this.applyViewport();
      return;
    }
    if (this.interaction.type === "drag") {
      const point = this.worldPoint(event);
      const dx = point.x - this.interaction.start.x;
      const dy = point.y - this.interaction.start.y;
      for (const id of this.interaction.ids) {
        this.view.nodes[id].x = this.interaction.originals[id].x + dx;
        this.view.nodes[id].y = this.interaction.originals[id].y + dy;
      }
      this.render();
      return;
    }
    if (this.interaction.type === "port") {
      const point = this.worldPoint(event);
      const nodeStyle = this.view.nodes[this.interaction.ownerId];
      this.view.ports[this.interaction.id] = {
        ...this.view.ports[this.interaction.id],
        ...nearestPerimeterPosition(nodeStyle, point),
      };
      this.render();
      return;
    }
    if (this.interaction.type === "resize") {
      const point = this.screenPoint(event);
      const dx = (point.x - this.interaction.start.x) / this.view.viewport.zoom;
      const dy = (point.y - this.interaction.start.y) / this.view.viewport.zoom;
      const style = this.view.nodes[this.interaction.id];
      style.width = Math.max(MIN_NODE_WIDTH, this.interaction.original.width + dx);
      style.height = Math.max(MIN_NODE_HEIGHT, this.interaction.original.height + dy);
      this.render();
    }
  }

  onPointerUp(event) {
    if (!this.interaction || event.pointerId !== this.interaction.pointerId) {
      return;
    }
    this.interaction = null;
    if (this.svg.hasPointerCapture(event.pointerId)) {
      this.svg.releasePointerCapture(event.pointerId);
    }
    this.notifyViewChanged(true);
  }

  toggleCollapse(id) {
    this.view.nodes[id].collapsed = !this.view.nodes[id].collapsed;
    this.render();
    this.callbacks.onSelectionChange?.(this.selection);
    this.notifyViewChanged(true);
  }

  screenPoint(event) {
    const rect = this.host.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
  }

  worldPoint(event) {
    const point = this.screenPoint(event);
    return {
      x: (point.x - this.view.viewport.x) / this.view.viewport.zoom,
      y: (point.y - this.view.viewport.y) / this.view.viewport.zoom,
    };
  }

  notifyViewChanged(persist) {
    this.callbacks.onViewChange?.(this.view, persist);
  }
}

function localPortPoint(style, direction, index, count, portStyle = null) {
  if (portStyle?.side) {
    return perimeterPoint(style, portStyle.side, portStyle.offset);
  }
  const usableTop = Math.min(128, style.height * 0.52);
  const usableHeight = Math.max(20, style.height - usableTop - 20);
  const y = usableTop + usableHeight * ((index + 1) / (count + 1));
  return { x: direction === "slv" ? 0 : style.width, y, side: direction === "slv" ? "left" : "right" };
}

function perimeterPoint(style, side, offset) {
  const ratio = clamp(Number(offset) || 0.5, 0.08, 0.92);
  if (side === "top" || side === "bottom") {
    return {
      x: style.width * ratio,
      y: side === "top" ? 0 : style.height,
      side,
    };
  }
  return {
    x: side === "left" ? 0 : style.width,
    y: style.height * ratio,
    side,
  };
}

function portLabelPoint(point) {
  if (point.side === "top") {
    return { x: point.x, y: point.y + 18, anchor: "middle" };
  }
  if (point.side === "bottom") {
    return { x: point.x, y: point.y - 10, anchor: "middle" };
  }
  return {
    x: point.x + (point.side === "left" ? 12 : -12),
    y: point.y + 3,
    anchor: point.side === "left" ? "start" : "end",
  };
}

function nearestPerimeterPosition(style, point) {
  const localX = clamp(point.x - style.x, 0, style.width);
  const localY = clamp(point.y - style.y, 0, style.height);
  const distances = [
    ["left", localX],
    ["right", style.width - localX],
    ["top", localY],
    ["bottom", style.height - localY],
  ];
  const side = distances.reduce((nearest, candidate) =>
    candidate[1] < nearest[1] ? candidate : nearest,
  )[0];
  const offset = side === "left" || side === "right"
    ? localY / style.height
    : localX / style.width;
  return { side, offset: clamp(offset, 0.08, 0.92) };
}

function svgElement(name, attributes = {}) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) {
    element.setAttribute(key, String(value));
  }
  return element;
}

function textElement(content, x, y, attributes = {}) {
  const element = svgElement("text", { x, y, ...attributes });
  element.textContent = content;
  return element;
}

function trimLabel(value, maxLength) {
  const text = String(value);
  return text.length <= maxLength ? text : `${text.slice(0, maxLength - 1)}…`;
}

function trimMiddle(value, maxLength) {
  const text = String(value);
  if (text.length <= maxLength) {
    return text;
  }
  const half = Math.floor((maxLength - 1) / 2);
  return `${text.slice(0, half)}…${text.slice(-half)}`;
}

function clamp(value, minimum, maximum) {
  return Math.min(maximum, Math.max(minimum, value));
}
