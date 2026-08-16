import {
  applyHierarchicalLayout,
  buildArchitecture,
  createDefaultView,
  mergeView,
} from "./model.js";
import { GraphEditor } from "./graph-editor.js";
import { deleteLayout, loadLayout, saveLayout } from "./storage.js";
import { downloadText, fingerprint, parseYaml, stringifyYaml } from "./yaml.js";

const elements = {
  graphHost: document.querySelector("#graph-host"),
  hierarchyTree: document.querySelector("#hierarchy-tree"),
  treeSearch: document.querySelector("#tree-search"),
  inspector: document.querySelector("#inspector"),
  yamlFile: document.querySelector("#yaml-file"),
  viewFile: document.querySelector("#view-file"),
  documentName: document.querySelector("#document-name"),
  documentId: document.querySelector("#document-id"),
  nodeCount: document.querySelector("#node-count"),
  connectionCount: document.querySelector("#connection-count"),
  zoomValue: document.querySelector("#zoom-value"),
  statusText: document.querySelector("#status-text"),
  statusIndicator: document.querySelector("#status-indicator"),
  saveStatus: document.querySelector("#save-status"),
  toastRegion: document.querySelector("#toast-region"),
};

const state = {
  architecture: null,
  view: null,
  editor: null,
  filename: "simple_chip.yaml",
  sourceText: "",
  fingerprint: "",
  storageKey: "",
  saveTimer: null,
  selection: null,
};

bindToolbar();
loadExample();

function bindToolbar() {
  document.querySelector("#open-yaml").addEventListener("click", () => elements.yamlFile.click());
  document.querySelector("#load-example").addEventListener("click", loadExample);
  document.querySelector("#import-view").addEventListener("click", () => elements.viewFile.click());
  document.querySelector("#export-view").addEventListener("click", exportView);
  document.querySelector("#auto-layout").addEventListener("click", () => {
    if (!state.architecture) return;
    applyHierarchicalLayout(state.architecture, state.view);
    state.editor.render();
    state.editor.fitView();
    scheduleSave();
    toast("已重新生成层次化布局");
  });
  document.querySelector("#fit-view").addEventListener("click", () => state.editor?.fitView());
  document.querySelector("#reset-view").addEventListener("click", resetView);
  document.querySelector("#zoom-in").addEventListener("click", () => {
    state.editor?.setZoom(state.view.viewport.zoom * 1.15);
  });
  document.querySelector("#zoom-out").addEventListener("click", () => {
    state.editor?.setZoom(state.view.viewport.zoom / 1.15);
  });
  elements.yamlFile.addEventListener("change", async (event) => {
    const file = event.target.files?.[0];
    if (file) {
      await loadDocument(await file.text(), file.name);
    }
    event.target.value = "";
  });
  elements.viewFile.addEventListener("change", async (event) => {
    const file = event.target.files?.[0];
    if (file) {
      await importView(await file.text());
    }
    event.target.value = "";
  });
  elements.treeSearch.addEventListener("input", renderTree);
  window.addEventListener("resize", () => state.editor?.applyViewport());
}

async function loadExample() {
  try {
    setStatus("busy", "正在载入示例架构");
    const response = await fetch("./examples/simple_chip.yaml", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    await loadDocument(await response.text(), "simple_chip.yaml");
  } catch (error) {
    reportError("示例 YAML 加载失败，请通过 HTTP 服务打开页面", error);
  }
}

async function loadDocument(text, filename) {
  try {
    setStatus("busy", `正在解析 ${filename}`);
    const architecture = buildArchitecture(parseYaml(text));
    const sourceFingerprint = await fingerprint(text);
    const storageKey = `yaml-v1:${sourceFingerprint}`;
    const defaultView = createDefaultView(architecture);
    defaultView.source.fingerprint = sourceFingerprint;
    const savedView = await loadLayout(storageKey);
    const view = mergeView(defaultView, savedView);

    state.architecture = architecture;
    state.view = view;
    state.filename = filename;
    state.sourceText = text;
    state.fingerprint = sourceFingerprint;
    state.storageKey = storageKey;
    state.selection = null;

    if (!state.editor) {
      state.editor = new GraphEditor(elements.graphHost, architecture, view, {
        onSelectionChange: handleSelection,
        onViewChange: (_nextView, persist) => {
          if (persist) scheduleSave();
        },
        onZoomChange: (zoom) => {
          elements.zoomValue.textContent = `${Math.round(zoom * 100)}%`;
        },
      });
    } else {
      state.editor.replaceDocument(architecture, view);
    }

    elements.documentName.textContent = filename;
    elements.documentId.textContent = `sha256 ${sourceFingerprint.slice(0, 12)}…`;
    elements.nodeCount.textContent = String(architecture.nodes.length);
    elements.connectionCount.textContent = `${architecture.connections.length} connections`;
    elements.treeSearch.value = "";
    renderTree();
    renderInspector(null);
    setStatus("ready", savedView ? "已恢复上次布局" : "YAML 已载入并完成校验");

    if (!savedView) {
      requestAnimationFrame(() => state.editor.fitView());
    }
    for (const warning of architecture.warnings) {
      toast(warning, "warning");
    }
  } catch (error) {
    reportError(`无法载入 ${filename}`, error);
  }
}

function handleSelection(selection) {
  state.selection = selection;
  renderTree();
  renderInspector(selection);
}

function renderTree() {
  if (!state.architecture) {
    elements.hierarchyTree.replaceChildren();
    return;
  }
  const query = elements.treeSearch.value.trim().toLowerCase();
  const fragment = document.createDocumentFragment();
  appendTreeNode(fragment, state.architecture.rootId, 0, query);
  elements.hierarchyTree.replaceChildren(fragment);
}

function appendTreeNode(parentElement, nodeId, depth, query) {
  const node = state.architecture.nodeById.get(nodeId);
  const style = state.view.nodes[nodeId];
  const childIds = state.architecture.childrenByParent.get(nodeId) ?? [];
  const matches = !query || [style.label, style.type_label, style.variable_label, node.id]
    .some((value) => String(value).toLowerCase().includes(query));
  const childMatches = childIds.some((id) => subtreeMatches(id, query));
  if (!matches && !childMatches) {
    return;
  }

  const row = document.createElement("button");
  row.className = `tree-row ${state.selection?.type === "node" && state.selection.id === nodeId ? "selected" : ""}`;
  row.style.paddingLeft = `${8 + depth * 16}px`;
  row.title = node.id;
  row.innerHTML = `
    <span class="tree-caret">${childIds.length ? "▾" : ""}</span>
    <i class="tree-kind ${node.kind}"></i>
    <span class="tree-label">${escapeHtml(style.label)}</span>
  `;
  row.addEventListener("click", () => state.editor.focusNode(nodeId));
  parentElement.append(row);

  if (!state.view.nodes[nodeId].collapsed || query) {
    childIds.forEach((id) => appendTreeNode(parentElement, id, depth + 1, query));
  }
}

function subtreeMatches(nodeId, query) {
  if (!query) return true;
  const node = state.architecture.nodeById.get(nodeId);
  const style = state.view.nodes[nodeId];
  if ([style.label, style.type_label, style.variable_label, node.id]
    .some((value) => String(value).toLowerCase().includes(query))) {
    return true;
  }
  return (state.architecture.childrenByParent.get(nodeId) ?? []).some((id) => subtreeMatches(id, query));
}

function renderInspector(selection) {
  if (!selection) {
    elements.inspector.className = "inspector empty-state";
    elements.inspector.innerHTML = `
      <div class="empty-icon">◇</div>
      <strong>选择一个模块或连线</strong>
      <p>在画布中单击对象，即可调整尺寸、颜色、形状与线条样式。</p>
    `;
    return;
  }
  elements.inspector.className = "inspector";
  if (selection.type === "node") {
    renderNodeInspector(selection.id);
  } else {
    renderConnectionInspector(selection.id);
  }
}

function renderNodeInspector(nodeId) {
  const node = state.architecture.nodeById.get(nodeId);
  const style = state.view.nodes[nodeId];
  elements.inspector.innerHTML = `
    <div class="selection-summary">
      <strong>${escapeHtml(style.label)}</strong>
      <span data-summary-variable>${escapeHtml(style.variable_label ?? node.id)}</span>
      <span><i data-summary-type>${escapeHtml(style.type_label ?? node.className)}</i> · ${node.kind}</span>
    </div>
    <div class="field-group">
      <h3>Appearance</h3>
      ${textField("type_label", "类型名称", style.type_label ?? node.className)}
      ${textField("variable_label", "变量名称", style.variable_label ?? node.id)}
      ${textField("label", "显示名称", style.label)}
      <div class="form-field">
        <label>形状</label>
        <select data-field="shape">
          ${option("rect", "直角矩形", style.shape)}
          ${option("rounded", "圆角矩形", style.shape)}
          ${option("capsule", "胶囊形", style.shape)}
          ${option("diamond", "菱形", style.shape)}
        </select>
      </div>
      ${colorField("fill", "填充颜色", style.fill)}
      ${colorField("border", "边框颜色", style.border)}
    </div>
    <div class="field-group">
      <h3>Geometry</h3>
      <div class="form-field inline">
        ${numberField("width", "宽度", Math.round(style.width), 170)}
        ${numberField("height", "高度", Math.round(style.height), 110)}
      </div>
      <div class="form-field inline">
        ${numberField("x", "X", Math.round(style.x))}
        ${numberField("y", "Y", Math.round(style.y))}
      </div>
      ${node.kind === "cluster" ? `
        <div class="checkbox-field">
          <input id="collapsed-field" data-field="collapsed" type="checkbox" ${style.collapsed ? "checked" : ""} />
          <label for="collapsed-field">折叠 Cluster</label>
        </div>` : ""}
    </div>
    <div class="field-group">
      <h3>Ports · ${node.ports.length}</h3>
      <div class="port-list">
        ${node.ports.map((port) => `
          <div class="port-item">
            <i class="${port.direction}"></i>
            <input
              class="port-name-input"
              data-port-label="${escapeAttribute(port.id)}"
              value="${escapeAttribute(state.view.ports[port.id]?.label ?? port.name)}"
              title="${escapeAttribute(port.id)}"
            />
            <b>${port.direction}</b>
          </div>`).join("") || "<span class='node-class'>No ports</span>"}
      </div>
    </div>
  `;

  elements.inspector.querySelectorAll("[data-field]").forEach((input) => {
    const eventName = input.type === "checkbox" || input.tagName === "SELECT" ? "change" : "input";
    input.addEventListener(eventName, () => {
      const field = input.dataset.field;
      if (input.type === "checkbox") {
        style[field] = input.checked;
      } else if (input.type === "number") {
        const minimum = field === "width" ? 170 : field === "height" ? 110 : -100000;
        style[field] = Math.max(minimum, Number(input.value));
      } else {
        style[field] = input.value;
      }
      syncColorInputs(input, elements.inspector);
      if (field === "label") {
        elements.inspector.querySelector(".selection-summary strong").textContent = style.label;
      } else if (field === "type_label") {
        elements.inspector.querySelector("[data-summary-type]").textContent = style.type_label;
      } else if (field === "variable_label") {
        elements.inspector.querySelector("[data-summary-variable]").textContent = style.variable_label;
      }
      state.editor.render();
      renderTree();
      scheduleSave();
    });
  });
  elements.inspector.querySelectorAll("[data-port-label]").forEach((input) => {
    input.addEventListener("input", () => {
      state.view.ports[input.dataset.portLabel].label = input.value;
      state.editor.render();
      scheduleSave();
    });
  });
}

function renderConnectionInspector(connectionId) {
  const connection = state.architecture.connections.find((item) => item.id === connectionId);
  const style = state.view.connections[connectionId];
  elements.inspector.innerHTML = `
    <div class="selection-summary">
      <strong>Connection</strong>
      <span>${escapeHtml(connection.from)}</span>
      <span>→ ${escapeHtml(connection.to)}</span>
    </div>
    <div class="field-group">
      <h3>Line Style</h3>
      ${colorField("color", "连线颜色", style.color)}
      ${numberField("width", "连线粗细", style.width, 1, 16)}
      <div class="form-field">
        <label>线型</label>
        <select data-field="line_style">
          ${option("solid", "实线", style.line_style)}
          ${option("dashed", "虚线", style.line_style)}
          ${option("dotted", "点线", style.line_style)}
        </select>
      </div>
    </div>
  `;
  elements.inspector.querySelectorAll("[data-field]").forEach((input) => {
    const eventName = input.tagName === "SELECT" ? "change" : "input";
    input.addEventListener(eventName, () => {
      style[input.dataset.field] = input.type === "number" ? Number(input.value) : input.value;
      syncColorInputs(input, elements.inspector);
      state.editor.render();
      scheduleSave();
    });
  });
}

async function importView(text) {
  if (!state.architecture) return;
  try {
    const imported = parseYaml(text);
    if (imported?.source?.fingerprint && imported.source.fingerprint !== state.fingerprint) {
      toast("布局文件来自不同 YAML，将只合并能够匹配的 dotted", "warning");
    }
    state.view = mergeView(createDefaultView(state.architecture), imported);
    state.view.source.fingerprint = state.fingerprint;
    state.editor.replaceDocument(state.architecture, state.view);
    renderTree();
    renderInspector(null);
    scheduleSave();
    toast("布局配置已导入");
  } catch (error) {
    reportError("布局文件导入失败", error);
  }
}

function exportView() {
  if (!state.view) return;
  const exported = JSON.parse(JSON.stringify(state.view));
  delete exported.id;
  delete exported.saved_at;
  const baseName = state.filename.replace(/\.(yaml|yml)$/i, "");
  downloadText(`${baseName}.wiepview.yaml`, stringifyYaml(exported));
  toast("布局文件已导出");
}

async function resetView() {
  if (!state.architecture) return;
  await deleteLayout(state.storageKey);
  state.view = createDefaultView(state.architecture);
  state.view.source.fingerprint = state.fingerprint;
  state.editor.replaceDocument(state.architecture, state.view);
  renderTree();
  renderInspector(null);
  requestAnimationFrame(() => state.editor.fitView());
  toast("已清除缓存并恢复默认布局");
}

function scheduleSave() {
  if (!state.storageKey || !state.view) return;
  elements.saveStatus.textContent = "正在保存…";
  clearTimeout(state.saveTimer);
  state.saveTimer = setTimeout(async () => {
    try {
      await saveLayout(state.storageKey, state.view);
      elements.saveStatus.textContent = "布局已自动保存";
    } catch (error) {
      elements.saveStatus.textContent = "自动保存失败";
      console.error(error);
    }
  }, 450);
}

function textField(field, label, value) {
  return `<div class="form-field"><label>${label}</label>` +
    `<input data-field="${field}" type="text" value="${escapeAttribute(value)}" /></div>`;
}

function numberField(field, label, value, min = -100000, max = 100000) {
  return `<div class="form-field"><label>${label}</label>` +
    `<input data-field="${field}" type="number" min="${min}" max="${max}" value="${value}" /></div>`;
}

function colorField(field, label, value) {
  return `<div class="form-field"><label>${label}</label><div class="color-input">` +
    `<input data-field="${field}" data-color-role="picker" type="color" value="${escapeAttribute(value)}" />` +
    `<input data-field="${field}" data-color-role="text" type="text" value="${escapeAttribute(value)}" />` +
    `</div></div>`;
}

function option(value, label, selected) {
  return `<option value="${value}" ${value === selected ? "selected" : ""}>${label}</option>`;
}

function syncColorInputs(changedInput, root) {
  const role = changedInput.dataset.colorRole;
  if (!role) return;
  const field = changedInput.dataset.field;
  const peerRole = role === "picker" ? "text" : "picker";
  const peer = root.querySelector(`[data-field="${field}"][data-color-role="${peerRole}"]`);
  if (peer && /^#[0-9a-f]{6}$/i.test(changedInput.value)) {
    peer.value = changedInput.value;
  }
}

function setStatus(type, message) {
  elements.statusIndicator.className = `status-indicator ${type}`;
  elements.statusText.textContent = message;
}

function reportError(message, error) {
  console.error(message, error);
  setStatus("error", message);
  toast(`${message}: ${error.message}`, "error", 7000);
}

function toast(message, type = "info", duration = 4000) {
  const element = document.createElement("div");
  element.className = `toast ${type}`;
  element.textContent = message;
  elements.toastRegion.append(element);
  setTimeout(() => element.remove(), duration);
}

function escapeHtml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function escapeAttribute(value) {
  return escapeHtml(value).replaceAll("`", "&#096;");
}
