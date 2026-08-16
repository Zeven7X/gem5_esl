const NODE_KINDS = new Set(["root", "cluster", "simobj"]);
const PORT_DIRECTIONS = new Set(["mst", "slv"]);

export function buildArchitecture(document) {
  if (!document || typeof document !== "object") {
    throw new Error("YAML 顶层必须是一个 map");
  }
  if (String(document.version) !== "1.0") {
    throw new Error(`暂不支持 YAML version: ${document.version ?? "missing"}`);
  }
  if (!Array.isArray(document.nodes) || !Array.isArray(document.connections)) {
    throw new Error("YAML 必须包含 nodes 和 connections 列表");
  }

  const warnings = [];
  const nodes = [];
  const nodeById = new Map();
  const portById = new Map();
  const childrenByParent = new Map();

  for (const rawNode of document.nodes) {
    const id = requiredString(rawNode.dotted, "node.dotted");
    if (nodeById.has(id)) {
      throw new Error(`重复节点 dotted: ${id}`);
    }
    const kind = requiredString(rawNode.kind, `${id}.kind`);
    if (!NODE_KINDS.has(kind)) {
      throw new Error(`节点 ${id} 的 kind 无效: ${kind}`);
    }

    const node = {
      id,
      name: requiredString(rawNode.name, `${id}.name`),
      kind,
      className: String(rawNode.class ?? "Unknown"),
      parentId: rawNode.parent == null ? null : String(rawNode.parent),
      ports: [],
    };

    for (const rawPort of rawNode.ports ?? []) {
      const portId = requiredString(rawPort.name, `${id}.port.name`);
      const direction = requiredString(rawPort.dir, `${portId}.dir`);
      if (portById.has(portId)) {
        throw new Error(`重复端口: ${portId}`);
      }
      if (!PORT_DIRECTIONS.has(direction)) {
        throw new Error(`端口 ${portId} 的 dir 无效: ${direction}`);
      }
      const port = {
        id: portId,
        ownerId: id,
        name: localName(portId),
        direction,
      };
      node.ports.push(port);
      portById.set(portId, port);
    }

    nodes.push(node);
    nodeById.set(id, node);
  }

  const roots = nodes.filter((node) => node.kind === "root");
  if (roots.length !== 1) {
    throw new Error(`必须且只能有一个 root，当前为 ${roots.length} 个`);
  }

  for (const node of nodes) {
    if (node.kind !== "root" && !nodeById.has(node.parentId)) {
      throw new Error(`节点 ${node.id} 的 parent 不存在: ${node.parentId}`);
    }
    const key = node.parentId ?? "__root__";
    const children = childrenByParent.get(key) ?? [];
    children.push(node.id);
    childrenByParent.set(key, children);
  }

  const connections = document.connections.map((rawConnection, index) => {
    const from = requiredString(rawConnection.from, `connections[${index}].from`);
    const to = requiredString(rawConnection.to, `connections[${index}].to`);
    const fromPort = portById.get(from);
    const toPort = portById.get(to);
    if (!fromPort || !toPort) {
      throw new Error(`连接端口不存在: ${from} -> ${to}`);
    }
    if (fromPort.direction === toPort.direction) {
      warnings.push(`连接方向可疑: ${from} -> ${to}`);
    }
    return {
      id: canonicalConnectionId(from, to, index),
      from,
      to,
    };
  });

  verifyAcyclic(roots[0].id, childrenByParent, nodes.length);

  if (document.node_count != null && Number(document.node_count) !== nodes.length) {
    warnings.push(`node_count=${document.node_count}，实际 nodes=${nodes.length}`);
  }
  if (
    document.connection_count != null &&
    Number(document.connection_count) !== connections.length
  ) {
    warnings.push(
      `connection_count=${document.connection_count}，实际 connections=${connections.length}`,
    );
  }

  return {
    version: "1.0",
    rootId: roots[0].id,
    nodes,
    connections,
    nodeById,
    portById,
    childrenByParent,
    warnings,
  };
}

export function createDefaultView(architecture) {
  const view = {
    view_version: "1.0",
    source: { root: architecture.rootId, fingerprint: "" },
    viewport: { x: 0, y: 0, zoom: 1 },
    nodes: {},
    connections: {},
  };

  for (const node of architecture.nodes) {
    view.nodes[node.id] = {
      x: 0,
      y: 0,
      width: node.kind === "simobj" ? 240 : 600,
      height: node.kind === "simobj" ? 150 : 420,
      label: node.name,
      shape: node.kind === "simobj" ? "rounded" : "rect",
      fill: defaultNodeColor(node.kind),
      border: defaultNodeBorder(node.kind),
      collapsed: false,
    };
  }

  for (const connection of architecture.connections) {
    view.connections[connection.id] = {
      color: "#3e6653",
      width: 3,
      line_style: "solid",
    };
  }

  applyHierarchicalLayout(architecture, view);
  return view;
}

export function applyHierarchicalLayout(architecture, view) {
  const root = architecture.nodeById.get(architecture.rootId);
  const rootView = view.nodes[root.id];
  rootView.x = 70;
  rootView.y = 60;

  layoutContainer(architecture, view, root.id, rootView.x, rootView.y, 0);
  view.viewport = { x: 0, y: 0, zoom: 0.82 };
}

function layoutContainer(architecture, view, parentId, baseX, baseY, depth) {
  const childIds = architecture.childrenByParent.get(parentId) ?? [];
  const clusters = childIds
    .map((id) => architecture.nodeById.get(id))
    .filter((node) => node.kind === "cluster");
  const objects = childIds
    .map((id) => architecture.nodeById.get(id))
    .filter((node) => node.kind === "simobj");

  const padding = 54;
  const header = 88;
  let cursorX = baseX + padding;
  let cursorY = baseY + header;
  let maxBottom = cursorY;

  for (const cluster of clusters) {
    const item = view.nodes[cluster.id];
    item.x = cursorX;
    item.y = cursorY;
    item.width = 700;
    item.height = 390;
    layoutContainer(architecture, view, cluster.id, item.x, item.y, depth + 1);
    cursorX += item.width + 70;
    maxBottom = Math.max(maxBottom, item.y + item.height);
  }

  if (clusters.length && depth > 0) {
    cursorY = maxBottom + 70;
    cursorX = baseX + padding;
  }

  objects.forEach((node, index) => {
    const item = view.nodes[node.id];
    const column = index % 4;
    const row = Math.floor(index / 4);
    item.x = cursorX + column * 290;
    item.y = cursorY + row * 210;
    maxBottom = Math.max(maxBottom, item.y + item.height);
  });

  const parent = view.nodes[parentId];
  if (parent) {
    const objectColumns = Math.min(Math.max(objects.length, 1), 4);
    const contentWidth = Math.max(
      clusters.reduce((sum, node) => sum + view.nodes[node.id].width + 70, -70),
      objectColumns * 290 - 50,
      520,
    );
    parent.width = Math.max(parent.width, contentWidth + padding * 2);
    const maxRight = childIds.reduce((right, id) => {
      const child = view.nodes[id];
      return Math.max(right, child.x + child.width);
    }, baseX + parent.width);
    parent.width = Math.max(parent.width, maxRight - baseX + padding);
    parent.height = Math.max(parent.height, maxBottom - baseY + padding);
    if (depth === 0) {
      parent.width = Math.max(parent.width, 1280);
      parent.height = Math.max(parent.height, 760);
    }
  }
}

export function mergeView(defaultView, savedView) {
  if (!savedView || savedView.view_version !== "1.0") {
    return defaultView;
  }
  const merged = structuredCloneSafe(defaultView);
  merged.viewport = { ...merged.viewport, ...(savedView.viewport ?? {}) };
  for (const [id, style] of Object.entries(savedView.nodes ?? {})) {
    if (merged.nodes[id]) {
      merged.nodes[id] = { ...merged.nodes[id], ...style };
    }
  }
  for (const [id, style] of Object.entries(savedView.connections ?? {})) {
    if (merged.connections[id]) {
      merged.connections[id] = { ...merged.connections[id], ...style };
    }
  }
  return merged;
}

export function descendantsOf(architecture, nodeId) {
  const result = [];
  const stack = [...(architecture.childrenByParent.get(nodeId) ?? [])];
  while (stack.length) {
    const id = stack.pop();
    result.push(id);
    stack.push(...(architecture.childrenByParent.get(id) ?? []));
  }
  return result;
}

function verifyAcyclic(rootId, childrenByParent, expectedCount) {
  const visited = new Set();
  const stack = [rootId];
  while (stack.length) {
    const id = stack.pop();
    if (visited.has(id)) {
      throw new Error(`层次结构存在循环或重复引用: ${id}`);
    }
    visited.add(id);
    stack.push(...(childrenByParent.get(id) ?? []));
  }
  if (visited.size !== expectedCount) {
    throw new Error(`存在不属于 root 的孤立节点，已访问 ${visited.size}/${expectedCount}`);
  }
}

function requiredString(value, field) {
  if (typeof value !== "string" || !value.trim()) {
    throw new Error(`${field} 必须是非空字符串`);
  }
  return value;
}

function localName(id) {
  return id.slice(id.lastIndexOf(".") + 1);
}

function canonicalConnectionId(from, to, index) {
  return `${[from, to].sort().join("|")}#${index}`;
}

function defaultNodeColor(kind) {
  return { root: "#f3efe3", cluster: "#dfe9df", simobj: "#fffaf0" }[kind];
}

function defaultNodeBorder(kind) {
  return { root: "#283c32", cluster: "#537461", simobj: "#9a6b38" }[kind];
}

function structuredCloneSafe(value) {
  return JSON.parse(JSON.stringify(value));
}
