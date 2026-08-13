# Wiep Platform Registry 设计与接入

## 1. 目标

Wiep Platform Registry 用于在 C++ 手工构建 gem5 Hardware 的过程中，
自动收集以下信息：

- `Hardware`、`WiepCluster` 和普通 `WiepModBase` IP 的层次结构。
- 每个关键 IP 拥有的 Master/Slave Port。
- Master Port 到 Slave Port 的实际 bind 连接。
- 可供后续可视化和手工修改的 YAML 中间文件。

长期数据流为：

```text
C++ Hardware 构造
        |
        v
WiepPlatformRegistry
        |
        v
Hardware YAML IR
        |
        +--> 可视化、布局和手工编辑
        |
        +--> 未来通过 Factory 重新构建 Hardware 和 Port 连接
```

当前阶段完成 C++ 到 YAML 的导出。YAML 到 C++ 的 Factory 构建将在后续
补充，但当前格式已经为它保留稳定 ID、`cpp_type`、`parameters` 和连接表。

## 2. 最小侵入原则

普通 IP 不需要逐个修改。自动登记集中在公共类中：

| 公共组件 | 职责 |
|---|---|
| `WiepModBase` | 构造时自动登记节点 |
| `WiepCluster` | 将已经登记的节点标记为 Cluster |
| `WiepMasterPort` | 构造时登记 Master Port，bind 后登记连接 |
| `WiepSlavePort` | 构造时登记 Slave Port |
| `WiepPlatformRegistry` | 保存 Node、Port、Connection 并输出 YAML |
| `Hardware` | 构造完成后显式调用一次 dump |

Registry 只保存对象的非拥有指针用于构造期查找，不负责删除 Hardware、
IP 或 Port。

## 3. 自动节点树

现有 `wiep_module_name` 使用层次栈生成完整名称，例如：

```text
hardware
hardware.ip0
hardware.cluster0
hardware.cluster0.ip1
hardware.cluster0.cluster1
hardware.cluster0.cluster1.ip2
```

`WiepModBase` 构造函数将完整名称注册为稳定 Node ID：

```cpp
m_platformNodeId =
    WiepPlatformRegistry::instance().registerNode(this, m_name);
```

Registry 通过最后一个 `.` 推导父节点：

```text
Node ID : hardware.cluster0.cluster1.ip2
Parent  : hardware.cluster0.cluster1
Name    : ip2
```

这依赖当前工程已经遵守的递归构造规则：

1. 父 Cluster 先进入构造。
2. 父 Cluster 在自身构造函数内创建子 IP 和子 Cluster。
3. `wiep_module_name` 的层次栈在子对象构造时仍然有效。

因此普通 IP 构造函数不需要增加 Registry 参数。

## 4. WiepCluster

`WiepCluster` 是 `WiepModBase` 的轻量子类：

```cpp
class WiepCluster : public WiepModBase
{
  public:
    WiepCluster(const wiep_module_name &name,
                const WiepModBaseParams *params)
        : WiepModBase(name, params)
    {
        WiepPlatformRegistry::instance().markCluster(this);
    }
};
```

基类先把对象登记为普通 Module，`WiepCluster` 构造随后将其标记为 Cluster。
建议顶层 `Hardware` 同样继承 `WiepCluster`，使其成为 YAML 根节点。

## 5. Registry 数据模型

Registry 保存三张逻辑表。

### 5.1 Node

```cpp
struct NodeRecord
{
    std::string id;
    std::string name;
    std::string parentId;
    std::string cppType;
    NodeKind kind;
    const WiepModBase *object;
};
```

`object` 只用于运行时查找，不会输出到 YAML。

### 5.2 Port

```cpp
struct PortRecord
{
    std::string id;
    std::string ownerId;
    std::string name;
    std::string protocol;
    PortDirection direction;
    std::int32_t index;
    const void *object;
};
```

Port ID 由 Owner Node ID、Port 名称和可选 index 组成：

```text
hardware.core0.m_axi
hardware.xbar.input[0]
hardware.xbar.input[1]
```

### 5.3 Connection

```cpp
struct ConnectionRecord
{
    std::string id;
    std::string sourcePortId;
    std::string targetPortId;
    std::string protocol;
};
```

节点层次是树，Port 连接是独立的图边。跨 Cluster 连接不进入 children
树，而是统一保存在 `connections` 中。

## 6. Port 构造函数接入

当前仓库没有用户实际的 `WiepMasterPort` / `WiepSlavePort` 定义，因此
Registry 提供了完整 API，但需要在实际旧 gem5/WIEP 工程中加入以下代码。

Master Port 构造函数：

```cpp
WiepModBase *wiepOwner =
    dynamic_cast<WiepModBase *>(owner);
WIEP_ASSERT(wiepOwner != NULL);

WiepPlatformRegistry::instance().registerPort(
    this,
    wiepOwner,
    name,
    WiepPlatformRegistry::PortDirection::Master,
    id == InvalidPortID
        ? WiepPlatformRegistry::NoPortIndex
        : static_cast<std::int32_t>(id),
    "axi");
```

Slave Port 构造函数：

```cpp
WiepModBase *wiepOwner =
    dynamic_cast<WiepModBase *>(owner);
WIEP_ASSERT(wiepOwner != NULL);

WiepPlatformRegistry::instance().registerPort(
    this,
    wiepOwner,
    name,
    WiepPlatformRegistry::PortDirection::Slave,
    id == InvalidPortID
        ? WiepPlatformRegistry::NoPortIndex
        : static_cast<std::int32_t>(id),
    "axi");
```

如果 Port 名称已经是完整名，例如 `hardware.core0.m_axi`，Registry 会移除
Owner 前缀并仍生成同一个稳定 ID，避免名称重复。

## 7. bind 连接登记

只在 Master 侧的自定义 `bind()` 登记，避免两端重复：

```cpp
void
WiepMasterPort::bind(Port &peer)
{
    MasterPort::bind(peer);

    WiepPlatformRegistry::instance().registerConnection(
        this,
        &peer,
        "axi");
}
```

顺序必须是：

1. 调用 gem5 基类 `bind()`。
2. 基类 bind 成功后登记连接。

如果底层 bind 失败，则 Registry 不应留下虚假连接。

Registry 会检查：

- 两端 Port 必须已经登记。
- source 必须是 Master。
- target 必须是 Slave。
- 相同 source/target 的连接只保存一次。

如果现有 `WiepMasterPort` 没有覆盖 `bind()`，需要在类声明中加入：

```cpp
void bind(Port &peer) override;
```

## 8. Hardware 构造完成后 Dump

`WiepModBase` 提供便捷函数：

```cpp
bool dumpPlatformYaml(const std::string &path) const;
```

顶层推荐写法：

```cpp
Hardware::Hardware(const wiep_module_name &name,
                   const HardwareParams *params)
    : WiepCluster(name, params)
{
    // 递归创建所有 Cluster 和 IP。
    // 构造所有 Port。
    // 完成所有 bind。

    setPlatformCppType("Hardware");

    const bool dumped =
        dumpPlatformYaml("hardware.yaml");
    WIEP_ASSERT(dumped);
}
```

之后正常执行：

```cpp
simulate(10ms);
```

也可以直接调用 Registry：

```cpp
WiepPlatformRegistry::instance().dumpYaml(
    "hardware.yaml",
    platformNodeId());
```

## 9. YAML 格式

生成文件采用扁平表，而不是深度嵌套 children：

```yaml
schema_version: 1
platform:
  root_node: "hardware"

nodes:
  - id: "hardware"
    name: "hardware"
    kind: cluster
    parent: null
    cpp_type: "Hardware"
    parameters: {}
    visual: {}

  - id: "hardware.cluster0.core0"
    name: "core0"
    kind: module
    parent: "hardware.cluster0"
    cpp_type: ""
    parameters: {}
    visual: {}

ports:
  - id: "hardware.cluster0.core0.m_axi"
    owner: "hardware.cluster0.core0"
    name: "m_axi"
    index: null
    direction: master
    protocol: "axi"

  - id: "hardware.sram.s_axi"
    owner: "hardware.sram"
    name: "s_axi"
    index: null
    direction: slave
    protocol: "axi"

connections:
  - id: "hardware.cluster0.core0.m_axi->hardware.sram.s_axi"
    source: "hardware.cluster0.core0.m_axi"
    target: "hardware.sram.s_axi"
    protocol: "axi"
    parameters: {}
    visual: {}
```

扁平格式的优点：

- 根据 `parent` 可以恢复任意深度的 Cluster 树。
- 移动节点只需要修改 `parent`。
- Connection 可以跨任意 Cluster。
- 可视化工具容易建立 Node/Edge 图。
- 手工修改 YAML 时缩进层次简单。
- 未来加载时可以先构建所有节点，再统一处理连接。

`parameters` 用于未来保存仿真构造参数；`visual` 只保存坐标、尺寸、颜色和
折叠状态等视图信息，不能影响仿真语义。

## 10. 从 YAML 反向构建

未来反向构建建议分为两个阶段：

```text
阶段 1：按 parent 顺序创建 Hardware、Cluster、IP 和它们固有的 Port
阶段 2：查找 connections 中的 source/target Port 并执行 bind
```

节点创建不能依靠任意 C++ 类型名直接调用构造函数，需要显式 Factory：

```cpp
registry.registerFactory("Core", createCore);
registry.registerFactory("Sram", createSram);
registry.registerFactory("CoreCluster", createCoreCluster);
```

加载器的处理顺序：

1. 校验 `schema_version`。
2. 校验 Node、Port 和 Connection ID 唯一。
3. 根据 `parent` 对节点拓扑排序。
4. 通过 `cpp_type`/Factory 创建节点。
5. 验证每个 YAML Port 在对应 C++ 节点中真实存在。
6. 根据 `connections` 查找 source/target。
7. 检查 Master/Slave 方向和 protocol。
8. 调用 Master 的 `bind()`。

可视化界面可以修改连接，但不能创建一个 C++ IP 类中根本不存在的成员
Port，除非该 IP 明确支持动态 Vector Port。

## 11. 类型和参数限制

只修改 `WiepModBase` 可以自动得到：

- 节点 ID。
- 父子层次。
- Module/Cluster 类型。
- Port 归属。
- bind 连接。

但无法自动得到稳定的 Factory 类型和每个 IP 的构造参数。

当前提供：

```cpp
setPlatformCppType("Core");
```

该接口可以由少量工厂或顶层代码设置稳定类型名。不要直接将
`typeid(*object).name()` 当作 YAML Factory ID，因为不同编译器的名称不稳定。

构造参数将在后续设计统一的参数描述/Factory 注册时补充，不建议立即要求
每个 IP 手工改构造函数。

## 12. 当前文件与验证

实现文件：

- `my_module/include/WiepPlatformRegistry.h`
- `my_module/src/WiepPlatformRegistry.cpp`
- `my_module/include/WiepModBase.h`
- `my_module/SConscript`

已完成验证：

- C++14 严格编译：`-Wall -Wextra -Wpedantic -Werror`。
- 自动父 ID 推导。
- 完整 Port 名和局部 Port 名归一化。
- Master 到 Slave 连接检查。
- 重复连接去重。
- YAML 文件真实换行和字符转义。
- 使用 Ruby YAML 解析器完成实际解析。

当前未完成：

- 用户实际 `WiepMasterPort` / `WiepSlavePort` 文件接入。
- YAML 参数序列化。
- YAML 读取器和 IP Factory。
- 可视化工具。
