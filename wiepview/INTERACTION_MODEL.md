# WiepView 显示与折叠交互说明

本文档说明 WiepView 当前的显示别名、端口布局和 Cluster 折叠语义。架构数据
仍以 `chip_arch.yaml` v1.0 为准；本页描述的编辑结果只写入 ViewState，不修改
YAML 中用于连接和层次引用的真实 ID。

## 1. 显示别名

每个节点在 `view.nodes[nodeId]` 中保存三种可编辑文本：

| 字段 | 默认值 | 用途 |
| --- | --- | --- |
| `label` | YAML `node.name` | 节点主标题和层次树名称 |
| `type_label` | YAML `node.class` | 画布上的类型名称 |
| `variable_label` | YAML `node.dotted` | 画布上的变量名称 |

端口显示名保存在 `view.ports[portId].label`。修改这些字段不会改变 Node ID、
Port ID、Connection endpoint 或父子关系，因此已有连接不会因重命名而失效。

## 2. 端口位置

端口布局保存在：

```yaml
ports:
  HARDWARE.system_xbar.s_axi:
    label: slave_in
    side: top
    offset: 0.56
```

- `side` 为 `left`、`right`、`top` 或 `bottom`。
- `offset` 是端口在对应边上的相对位置，范围限制为 `0.08` 到 `0.92`。
- 拖动彩色端口圆点时，端口自动吸附到最近的节点边缘。
- 使用相对位置而非绝对坐标，因此节点缩放后端口仍保持合理位置。
- 可见圆点上方有透明拖拽热区，避免连接线的点击区域遮挡端口。

端口名称和位置通过 IndexedDB 自动保存，也会包含在导出的
`*.wiepview.yaml` sidecar 文件中。旧布局没有 `ports` 字段时，会自动合并默认值。

## 3. Cluster 折叠语义

Cluster 折叠不是删除内部对象，也不修改 Architecture。渲染规则如下：

1. Cluster 的全部后代 Node 不渲染。
2. 两端都位于该 Cluster 子树中的内部 Connection 不渲染。
3. 只有一端位于子树中的跨边界 Connection 继续渲染。
4. 跨边界连接的隐藏端点代理到折叠 Cluster 的可见端口。
5. 展开 Cluster 后，连接重新使用内部真实端口。

因此移动折叠 Cluster 上的可见端口，会直接改变跨边界连线的显示端点；内部
Connection 和真实 endpoint 始终保持不变。

## 4. 折叠端口代理规则

隐藏端口映射到可见 Cluster 端口时按以下顺序选择：

1. 过滤出方向相同的端口，即 `mst` 只匹配 `mst`，`slv` 只匹配 `slv`。
2. 优先匹配局部端口名相同的端口，例如内部 `local_xbar.m_axi` 映射到
   Cluster 的 `m_axi`。
3. 如果该方向只有一个 Cluster 端口，自动使用这个唯一端口。
4. 如果有多个同方向端口且名称无法匹配，则退回 Cluster 对应方向的边缘中点。

第 4 种情况没有足够信息进行可靠推断。后续 schema 如需支持复杂多出口
Cluster，应增加显式 boundary/proxy port 映射，而不是继续依靠名称猜测。

## 5. 相关代码

- `src/model.js`：创建和合并 Node/Port ViewState。
- `src/app.js`：Inspector 编辑、层次树显示和自动保存。
- `src/graph-editor.js`：端口绘制、拖动、折叠过滤和代理端点计算。
- `styles.css`：端口编辑框与拖拽热区样式。

## 6. 回归结果

使用 `examples/simple_chip.yaml` 验证：

- 展开状态为 7 个可见节点、4 条可见连接。
- 折叠 `CPU Cluster` 后为 4 个可见节点、2 条可见外部连接。
- 内部两条 Core 到 Local XBAR 的连接不再形成自连接横线。
- 拖动折叠后的 `CPU_CLUSTER.m_axi` 时，连接 System XBAR 的曲线端点同步移动。
- 修改类型、变量和端口显示名后，刷新页面可以从 IndexedDB 恢复。
- 浏览器控制台无 error 或 warning。
