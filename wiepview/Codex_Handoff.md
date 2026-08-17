# gem5 可视化项目 Codex 交接说明

## 1. 使用方式

本文件用于把旧对话的有效上下文交接给新对话 **gem5可视化(2)**。

新对话开始工作前，应优先阅读：

1. 本文件 `wiepview/Codex_Handoff.md`。
2. `wiepview/README.md`。
3. `my_module/WiepChipVisualization.md`。
4. 与具体任务有关的源码，不要仅根据历史描述猜测当前实现。

## 2. 仓库信息

```text
本地目录：/Users/zevenkk/CLionProjects/gem5_project
远端仓库：git@github.com:Zeven7X/gem5_esl.git
GitHub：https://github.com/Zeven7X/gem5_esl
当前分支：main
```

可视化工程对应提交：

```text
c5812cc Improve WiepView port and cluster interactions
96d505f Add WiepView chip architecture visualizer
6e81687 Refine chip visualization architecture document
f2b2474 Add platform topology registry and YAML export
```

## 3. 项目背景

仓库根目录包含 gem5 原生源码。CMake 的用途主要是让 CLion 索引 gem5 和用户
代码，不用于替代 gem5 官方 SCons 构建系统。

用户的芯片仿真平台主要使用 C++ 手工构建：

- 顶层对象名为 `Hardware`。
- 关键 IP 继承 `WiepModBase`，而 `WiepModBase` 基本继承自 gem5 `SimObject`。
- `WiepCluster` 是 `WiepModBase` 的轻量级派生类，只用于描述层次节点。
- 关键端口继承 gem5 `Port` 体系中的 Master/Slave Port。
- IP/Cluster 构成父子树，Port bind 构成独立的随机连接图。
- `Hardware` 构造、Port bind 和 `initAll()` 完成后，显式 dump YAML，然后再
  `simulate(...)`。

## 4. 用户明确约束

- gem5 完整构建继续使用官方 SCons，不要用 CMake 重建整个 gem5。
- 不要把 gem5 所有 `.cc/.cpp` 塞入单个 CMake target。
- 尽量不修改 gem5 原生目录和框架。
- 用户主要使用 Linux、GCC 13.1 及以下、C++14。
- C++ 代码不能依赖 C++17，例如不能使用 `std::byte`。
- 用户会逐步指导 Wiep AXI/多进程代码修改；没有明确要求时，不要主动重写
  `WiepAxiMst.h`、`WiepAxiSlv.h`、`WiepTlmFifo.h`。
- 提交 Git 时必须限制路径，避免夹带无关的本地修改。

## 5. 平台拓扑注册器

相关文件：

```text
my_module/include/WiepPlatformRegistry.h
my_module/src/WiepPlatformRegistry.cpp
my_module/include/WiepModBase.h
my_module/WiepPlatformRegistry_Design.md
```

设计目标：

- 在 `WiepModBase` 构造时自动注册 Node。
- 使用节点名字和 Cluster 关系记录层次结构。
- 在 Master/Slave Port 构造或 bind 时注册 Port 和 Connection。
- 在所有对象和连接完成后，由 `Hardware` 显式 dump YAML。
- Node 层次和 Port 连接分开保存：层次是树，Connection 是图。

当前仓库内的 Registry 原型和另一环境已跑通的 YAML 生成器并非完全相同。
可视化工具当前以 `my_module/WiepChipVisualization.md` 记录的 YAML v1.0 为准。

## 6. YAML v1.0 核心格式

```yaml
version: '1.0'

root:
  name: 'Hardware'
  dotted: 'HARDWARE'
  kind: root

nodes:
  - dotted: 'HARDWARE'
    name: 'Hardware'
    kind: root
    class: 'Hardware'
    parent: null
    ports: []

connections: []

node_count: 1
connection_count: 0
```

关键规则：

- `nodes` 是扁平列表，使用 `parent` 恢复 Cluster 树。
- `kind` 为 `root`、`cluster` 或 `simobj`。
- `dotted` 是 v1.0 内 Node/Port/Connection 的引用键。
- Port 使用完整 dotted 名，并包含 `dir: mst|slv`。
- Connection 使用 `from` 和 `to` 引用两个 Port。
- 当前 Connection 视为无向边，Master/Slave 方向由 Port 的 `dir` 推导。
- 当前 `dotted` 同时编码层次路径，因此移动或重命名 Cluster 会改变后代 ID。

完整规范、已知问题和未来候选 Schema 见：

```text
my_module/WiepChipVisualization.md
```

## 7. WiepView 当前实现

可视化工程已经从 `my_module` 迁出，作为与其同级的独立目录：

```text
wiepview/
├── README.md
├── INTERACTION_MODEL.md
├── index.html
├── styles.css
├── serve.sh
├── favicon.svg
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

这是一个零构建依赖的静态网页：

- 原生 HTML/CSS/ES Modules。
- SVG 实现架构图画布。
- 固定版本的 `js-yaml 4.1.0` 已放在 `vendor/`，运行时不访问 CDN。
- 不需要 Node/npm，使用 Python HTTP Server 即可运行。

## 8. 已实现功能

- 导入并校验 YAML v1.0。
- 展示 Root、Cluster、SimObject、Master/Slave Port 和 Connection。
- 按 `parent` 恢复层次树。
- Node 拖动；拖动 Cluster 时同步移动全部后代。
- 拖动右下角控制点改变 Node 大小。
- 分别修改 Node 的显示名称、类型名称和变量名称，不改变真实 ID。
- 修改填充色、边框色、位置、尺寸和形状。
- 形状包括直角矩形、圆角矩形、胶囊形和菱形。
- 修改 Port 显示名，不改变 Connection 使用的真实 Port ID。
- 将 Port 拖动并吸附到 Node 的左、右、上、下四条边。
- Port 位置使用 `side + offset` 保存，Node 缩放后仍保持相对位置。
- 修改 Connection 的颜色、粗细、实线/虚线/点线。
- Cluster 展开和折叠；折叠后隐藏内部 Node 与内部 Connection。
- 跨 Cluster 边界的 Connection 代理到 Cluster 的同方向可见 Port。
- 代理优先使用同局部名 Port，其次使用唯一同方向 Port，无法可靠推导时使用
  对应方向的边缘中点。
- 层次树搜索和 Node 定位。
- 层次化自动布局、画布平移、滚轮缩放和适应画布。
- IndexedDB 自动保存布局；失败时回退到 `localStorage`。
- 根据原始 YAML 的 SHA-256 指纹恢复对应布局。
- 导入和导出 `*.wiepview.yaml` sidecar 布局文件。

## 9. 数据边界

当前明确将架构语义和视图状态分离：

```text
chip_arch.yaml
负责 Node、Cluster、Port、Connection 等仿真架构事实

chip_arch.wiepview.yaml
负责显示别名、坐标、尺寸、颜色、形状、Port 布局、线宽、折叠状态和 viewport
```

这样 C++ 重新 dump 架构时，不会直接覆盖用户调整的布局。

当前 ViewState 使用 v1.0 `dotted` 关联 Node。未来支持“可视化编辑 -> YAML ->
C++ Hardware”时，应引入不会随移动或改名变化的稳定 Node、Port、Connection ID。

## 10. 示例架构

`wiepview/examples/simple_chip.yaml` 包含：

```text
Hardware (root)
├── CPU Cluster (cluster)
│   ├── Core 0
│   ├── Core 1
│   └── Local XBAR
├── System XBAR
└── SRAM 1 MiB
```

数据流：

```text
Core 0 ─┐
        ├─> Local XBAR -> System XBAR -> SRAM
Core 1 ─┘
```

示例共 7 个 Node、4 条 Connection。

## 11. 运行和验证

在仓库根目录执行：

```bash
sh wiepview/serve.sh
```

浏览器打开：

```text
http://127.0.0.1:8080/
```

指定其他端口：

```bash
sh wiepview/serve.sh 9000
```

旧对话中已通过真实浏览器验证：

- 页面和所有静态资源正常加载。
- YAML 识别到 7 个 Node 和 4 条 Connection。
- 控制台无 error/warning。
- Node 改名和形状修改在刷新后成功恢复。
- Node 类型、变量和 Port 显示名在刷新后成功恢复。
- Port 可以拖动到四条边，Connection 端点同步更新。
- Cluster 折叠后可见节点从 7 个变为 4 个，可见 Connection 从 4 条变为 2 条。
- 折叠后内部 Connection 不渲染，跨边界 Connection 代理到 Cluster 可见 Port。
- 自动布局形成 `Core -> Local XBAR -> System XBAR -> SRAM` 的左到右数据流。

## 12. 当前 Git 工作区注意事项

生成本文件时，除已提交的 WiepView 外，本地还有以下独立改动：

```text
 M CMakeLists.txt
AM my_module/include/WiepAxiMst.h
AM my_module/include/WiepAxiSlv.h
AM my_module/include/WiepTlmFifo.h
```

这些文件与本次可视化交接无关。后续提交 WiepView 时应使用路径限定，例如：

```bash
git add -- wiepview
git commit --only -- wiepview
```

不要 reset、checkout 或覆盖这些用户改动。

## 13. 下一阶段建议

建议新对话按以下顺序继续：

1. 先让用户确认当前网页交互和视觉风格。
2. 使用用户真实 `chip_arch.yaml` 测试大规模 Node/Connection。
3. 优化大型图性能、Connection 路由、Cluster 嵌套和自动布局。
4. 增加撤销/重做、多选、对齐、复制和 SVG/PNG 导出。
5. 再进入创建/删除 Node、Port 和 Connection 的架构编辑模式。
6. 最后单独实现 YAML Schema v2、Factory Registry 和 C++ Hardware Loader。

不要在第一阶段承诺当前 YAML v1.0 可以完整反向生成 gem5 Hardware。

## 14. 给新对话的建议开场指令

可以在 **gem5可视化(2)** 中发送：

```text
请先阅读仓库中的 wiepview/Codex_Handoff.md、wiepview/README.md 和
my_module/WiepChipVisualization.md，再检查 wiepview 当前代码。不要修改或提交
CMakeLists.txt 与 my_module/include 下三个未完成的 Wiep 头文件。后续继续迭代
WiepView，并保持 YAML 架构语义与 ViewState 布局数据分离。
```
