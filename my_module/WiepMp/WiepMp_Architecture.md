# Wiep Multi-Process Architecture

## 1. 目标

本文档描述多个独立 gem5 进程之间的共享内存通信和固定周期同步方案。
整体分为两个相互独立的部分：

- 控制面：进程注册、启动、Epoch barrier、心跳、退出和错误传播。
- 数据面：进程对之间通过 POSIX 共享内存传输 AXI 命令。

共享内存中禁止保存 `PacketPtr`、`RequestPtr`、STL 容器、虚函数对象及
其他进程本地指针。每个进程独立管理 gem5 对象和 Packet 池。

## 2. 进程与接口拓扑

进程 0 作为 SoC 进程和同步协调者，其他进程可以表示 Core、加速器或
其他子系统。例如：

```text
SoC (P0) <-> Core0 (P1): Link 10
SoC (P0) <-> Core1 (P2): Link 20
```

一个 Link 固定连接两个进程，并可以包含 0 到 N 组 AXI 接口：

```text
Interface 100: P0 Master <-> P1 Slave
Interface 101: P0 Slave  <-> P1 Master
Interface 102: P0 Master <-> P1 Slave
```

每组接口包含五个逻辑通道：

```text
AW: Master -> Slave
W : Master -> Slave
AR: Master -> Slave
B : Slave  -> Master
R : Slave  -> Master
```

接口 ID 在同一次仿真中必须唯一。共享内存名称应根据 session 和 Link ID
统一生成，不能由两端分别随意配置。

## 3. 控制面

整个仿真 session 只创建一份控制共享内存，至少包含：

- magic、协议版本和配置哈希。
- 预期进程数量和同步周期。
- 全局状态、已完成 Epoch、终止标志和失败原因。
- 每个进程的 ID、名称、PID、状态、Tick、Epoch、心跳和错误码。
- 一个进程共享的 pthread mutex 和 condition variable。

mutex 和 condition variable 使用 `PTHREAD_PROCESS_SHARED` 初始化。Linux
下建议使用 robust mutex，以便检测持锁进程异常退出。

P0 创建并初始化控制区，其他进程等待并 attach。所有进程注册并进入
Ready 后，P0 才释放启动 barrier。

全局 barrier 只能由每个进程唯一的 Runtime 调用。每个 AXI Port 不能
分别执行全局同步。

## 4. Epoch 双缓冲数据面

为了保证确定性，每条单向 AXI 通道推荐使用两个 Epoch Buffer，而不是
让新写入的数据立即对对端可见：

```cpp
template <typename WirePacket, uint32_t Capacity>
struct SharedEpochChannel
{
    EpochBuffer<WirePacket, Capacity> buffers[2];
};

template <typename WirePacket, uint32_t Capacity>
struct EpochBuffer
{
    uint64_t epoch;
    uint32_t count;
    uint32_t published;
    WirePacket packets[Capacity];
};
```

Epoch N 期间：

```text
读取：buffers[N % 2]
写入：buffers[(N + 1) % 2]
```

严格执行以下顺序：

1. 进入 Epoch N。
2. 只读取一次 N 对应的输入 Buffer。
3. 运行本同步周期内的本地 gem5 事件。
4. 将本 Epoch 产生的输出编码到 N+1 Buffer。
5. 发布 N+1 Buffer。
6. 进入 barrier N。
7. P0 等所有进程到齐后释放 barrier。
8. 进入 Epoch N+1，读取刚刚发布的 Buffer。

写端使用 release 发布：

```cpp
__atomic_store_n(&buffer.published, 1U, __ATOMIC_RELEASE);
```

读端使用 acquire 检查：

```cpp
__atomic_load_n(&buffer.published, __ATOMIC_ACQUIRE);
```

如果业务保证每条通道每个 Epoch 最多一个包，可以简化为：

```cpp
template <typename WirePacket>
struct SharedEpochSlot
{
    uint64_t epoch;
    uint32_t valid;
    WirePacket packet;
};
```

如果 W/R burst 或一个 Epoch 内可能有多个命令，则应使用固定容量数组。

## 5. gem5 同步事件

每个进程只有一个周期性 `EventWrapper`。同步边界执行：

```text
发布所有 N+1 输出 Buffer
更新本地 Tick、Epoch 和 heartbeat
进入 ProcessManager barrier N
等待所有进程到达
本地 Epoch 加一
开放新 Epoch 的输入 Buffer
通知 AXI 模型可读数据或可写空间
调度下一个同步事件
```

同步事件必须在当前 Tick 的业务事件全部完成后执行，需要设置明确的
“本 Tick 最后执行”优先级，不能依赖默认优先级。

顶层可以调用较长时间的 `simulate(...)`。周期性同步事件会在内部按
配置的 Tick 边界阻塞 gem5 事件队列，其他进程到齐后再继续运行。

## 6. AXI FIFO 语义

MP AXI 接口可以保留原有调用形式：

```cpp
m_axiMst->m_arFifo->nbWrite(pkt);
m_axiSlv->m_rFifo->nbWrite(pkt);
```

发送方向的 `nbWrite(PacketPtr)` 应执行：

1. 检查本地 OSTD 和输出 Buffer 容量。
2. 将 Packet 编码为无指针 WirePacket。
3. 写入 N+1 的共享 Epoch Buffer。
4. 只有共享 Buffer 接收成功才返回 `true`。

接收方向的 `nbGet()` 或 `nbRead()` 应执行：

1. 只访问当前可读 Epoch 的 Buffer。
2. 将 WirePacket 解码成新的本地 `PacketPtr`。
3. 从当前进程的 Packet 池取得 Packet。
4. 解码成功后才能将共享条目标记为已消费。

输出 Buffer 满时，`nbWrite()` 返回 `false` 并记录被阻塞的 AXI 通道。
对端消费数据并完成下一次同步后，只对该通道触发一次 retry。

OSTD 计数、Packet 生命周期、AXI 顺序检查和业务回调都保留在本地进程。

## 7. 跨进程 Packet 格式

WirePacket 必须是 trivially-copyable 且不能包含指针。建议至少包含：

```cpp
struct WiepMpPacket
{
    uint32_t interfaceId;
    uint8_t axiChannel;
    uint8_t command;
    uint16_t flags;

    uint64_t tag;
    uint64_t address;
    uint64_t visibleEpoch;

    uint32_t size;
    uint32_t burstLength;
    uint32_t dataLength;

    uint8_t qos;
    uint8_t response;
    uint8_t data[MaxDataBytes];
};
```

模型负责实现：

```cpp
bool encodePacket(PacketPtr packet, WiepMpPacket &wire);
PacketPtr decodePacket(const WiepMpPacket &wire);
```

大数据可以放在固定共享内存池中，通过整数 index/handle 引用，禁止将
宿主机地址写入 WirePacket。

## 8. TOML 配置

所有进程读取同一份拓扑配置，只通过命令行指定本地 process ID：

```toml
[simulation]
session = "soc_run_001"
process_count = 3
sync_period_ticks = 1000
startup_timeout_seconds = 60
barrier_timeout_seconds = 60
protocol_version = 1

[[process]]
id = 0
name = "soc"

[[process]]
id = 1
name = "core0"

[[process]]
id = 2
name = "core1"

[[link]]
id = 10
name = "soc_core0"
process_a = 0
process_b = 1

[[link.interface]]
id = 100
name = "soc_mst0_core0_slv0"
process_a_role = "master"
depth_aw = 16
depth_w = 256
depth_b = 16
depth_ar = 16
depth_r = 256

[[link.interface]]
id = 101
name = "soc_slv0_core0_mst0"
process_a_role = "slave"
depth_aw = 16
depth_w = 256
depth_b = 16
depth_ar = 16
depth_r = 256

[[link]]
id = 20
name = "soc_core1"
process_a = 0
process_b = 2

[[link.interface]]
id = 200
name = "soc_mst0_core1_slv0"
process_a_role = "master"
depth_aw = 16
depth_w = 256
depth_b = 16
depth_ar = 16
depth_r = 256
```

启动示例：

```bash
./soc_sim   --config system.toml --process-id 0
./core0_sim --config system.toml --process-id 1
./core1_sim --config system.toml --process-id 2
```

每个进程单独需要的参数只有：

- 公共配置文件路径。
- 唯一 process ID。
- 可选 session-name 覆盖。
- 与多进程传输无关的本地模型参数。

进程数量、同步周期、Link 两端、接口 ID、AXI 角色、Buffer 深度、协议
版本和 WirePacket ABI 必须完全一致。解析 TOML 后应计算规范化配置哈希，
写入所有共享内存 Header，attach 时进行校验。

## 9. 启动与退出

推荐启动流程：

1. 所有进程解析 TOML 并计算相同的配置哈希。
2. P0 创建控制共享内存。
3. Worker attach 并注册。
4. 每个 Link 由两端 process ID 较小者创建数据共享内存。
5. 另一端 attach 并校验元数据。
6. 所有进程标记 Ready。
7. P0 释放启动 barrier。

推荐退出流程：

1. 每个进程报告 Finished 或 Failed。
2. 任一失败设置全局终止标志并唤醒所有等待者。
3. P0 等待所有进程退出或超时。
4. 仅由 P0 unlink 控制区和 Link 共享内存。

不要在任意 Link 端点析构函数中直接 unlink。较快进程可能在较慢进程
attach 前删除共享内存。每次运行应使用唯一 session，避免连接到旧对象。

## 10. 实现环境

- Linux。
- GCC 7.3 到 GCC 13.1。
- C++14。
- POSIX `shm_open`、`ftruncate`、`mmap` 和 `shm_unlink`。
- 控制面使用 process-shared pthread 同步。
- 数据发布使用 GCC `__atomic` acquire/release。

现有 `WiepProcessManager` 可以继续作为控制面状态机的参考。AXI 数据面
建议按照本文档的 Epoch 双缓冲规则重新实现。
