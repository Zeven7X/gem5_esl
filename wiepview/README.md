# WiepView

WiepView 是一个面向 gem5/Wiep 芯片架构 YAML 的浏览器可视化原型。它是独立
静态网页，不参与 gem5 的 SCons 或 CMake 构建。

## 当前能力

- 读取并校验 `chip_arch.yaml` v1.0。
- 展示 Root、Cluster、SimObject、Master/Slave Port 和 Connection。
- 拖动 Node；拖动 Cluster 时同步移动其全部后代。
- 拖动 Node 右下角修改尺寸。
- 修改 Node 显示名称、类型名称、变量名称、颜色、边框、形状、位置和尺寸。
- 修改端口显示名，并将端口拖动吸附到 Node 的四条边。
- 修改 Connection 颜色、粗细和线型。
- 双击 Cluster 或点击其右上角按钮进行展开/折叠；折叠后隐藏内部连线，跨边界
  连线代理到可移动的 Cluster 可见端口。
- 搜索并定位 Node。
- 自动生成层次布局、画布缩放和平移、适应画布。
- 使用 IndexedDB 自动恢复相同 YAML 的布局。
- 导入和导出可移植的 `*.wiepview.yaml` 布局文件。

## 运行

在仓库根目录执行：

```bash
sh wiepview/serve.sh
```

如需指定其他端口，可执行 `sh wiepview/serve.sh 9000`。

浏览器打开：

```text
http://127.0.0.1:8080/
```

页面会自动载入 `examples/simple_chip.yaml`。也可以通过“导入 YAML”选择其他
符合 v1.0 Schema 的文件。

## 文件结构

```text
wiepview/
├── index.html
├── styles.css
├── examples/
│   └── simple_chip.yaml
├── src/
│   ├── app.js
│   ├── graph-editor.js
│   ├── model.js
│   ├── storage.js
│   └── yaml.js
└── vendor/
    ├── js-yaml.min.js
    └── js-yaml.LICENSE
```

## 数据边界

原始架构 YAML 保存芯片语义；位置、尺寸、颜色和线条样式保存在独立 ViewState
中。浏览器缓存按原始 YAML 的 SHA-256 指纹区分，导出的 sidecar 文件默认命名
为 `原文件名.wiepview.yaml`。

当前以 v1.0 `dotted` 作为布局关联键。未来支持可编辑架构和反向构建 C++ 时，
需要迁移到稳定的 Node、Port、Connection ID，详见
`../my_module/WiepChipVisualization.md`。

显示别名、端口布局和 Cluster 折叠代理的实现规则详见
`INTERACTION_MODEL.md`。
