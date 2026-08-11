# Wiep AXI Multi-Process 原理与使用

## 1. 当前定位

`WiepAxiMstMp` 和 `WiepAxiSlvMp` 以正常版 `WiepAxiMstPort` /
`WiepAxiSlvPort` 为基础，保留原有本地 `WiepTlmFifo`、OSTD、统计和业务
回调逻辑，将跨 Port 的 AXI 传输替换成共享内存传输。

当前代码是后续接入真实模型的骨架：

- 支持 AW/W/B/AR/R 五条数据共享通道。
- 支持 Master Ready 和 Slave Ready 两条状态共享通道。
- 数据和流控状态按下一拍生效。
- 支持一对进程之间配置多组 Master/Slave。
- Packet 与 WirePacket 的字段转换仍是占位实现。
- 全局多进程 barrier 仍由外层 `WiepProcessManager` / Runtime 负责。

## 2. 一组接口的共享通道

每组逻辑 AXI 接口由一个唯一 `interfaceId` 标识，并使用 8 个连续的共享
通道编号，其中当前使用 7 个：

| Offset | 通道 | 方向 | 数据类型 |
|---:|---|---|---|
| 0 | AW | Master -> Slave | `WiepAxiAwMpPacket` |
| 1 | W | Master -> Slave | `WiepAxiWMpPacket` |
| 2 | B | Slave -> Master | `WiepAxiBMpPacket` |
| 3 | AR | Master -> Slave | `WiepAxiArMpPacket` |
| 4 | R | Slave -> Master | `WiepAxiRMpPacket` |
| 5 | Master Ready | Master -> Slave | `WiepAxiMstReadyMpPacket` |
| 6 | Slave Ready | Slave -> Master | `WiepAxiSlvReadyMpPacket` |
| 7 | 保留 | - | - |

实际共享通道 ID 为：

```cpp
channelId = interfaceId * 8 + channelOffset;
```

所有通道默认有效深度为 16。底层 `WiepSharedChannel` 使用一个保留槽区分
空和满，因此模板内部实际分配 `Depth + 1` 个槽。

## 3. Packet 跨进程转换

`PacketPtr` 只能在创建它的进程中使用，不能写入共享内存。发送端先调用
对应的占位转换函数：

```cpp
WiepAxiAwMpPacket packetToAwMp(PacketPtr pkt);
WiepAxiWMpPacket  packetToWMp(PacketPtr pkt);
WiepAxiBMpPacket  packetToBMp(PacketPtr pkt);
WiepAxiArMpPacket packetToArMp(PacketPtr pkt);
WiepAxiRMpPacket  packetToRMp(PacketPtr pkt);
```

接收端从本地 Packet 池取得新的 Packet：

```cpp
PacketPtr awMpToPacket(const WiepAxiAwMpPacket &wire);
PacketPtr wMpToPacket(const WiepAxiWMpPacket &wire);
PacketPtr bMpToPacket(const WiepAxiBMpPacket &wire);
PacketPtr arMpToPacket(const WiepAxiArMpPacket &wire);
PacketPtr rMpToPacket(const WiepAxiRMpPacket &wire);
```

当前函数只建立了接口，尚未真正复制 `wiepReq`、地址、tag、burst、QoS、
response 和 W/R payload。实际运行前必须完成这些函数。

WirePacket 必须保持 trivially-copyable，禁止包含指针、STL 容器和虚函数
对象。

## 4. 数据发送

Master 原有发送路径仍然调用：

```cpp
sendTimingReq(pkt);
```

MP 版本中的同名函数会：

1. 应用已经到下一拍的 Slave Ready 状态。
2. 根据 `pkt->wiepReq->getChanType()` 选择 AW、W 或 AR。
3. 检查对应的 `awReady`、`wReady` 或 `arReady`。
4. 将 Packet 转换成对应 WirePacket。
5. 设置 `visibleTick = curTick() + clockPeriod()`。
6. 尝试写入对应共享通道。

Slave 的 `sendTimingResp(pkt)` 对 B/R 执行相同流程，并检查 Master 发来的
`bReady` / `rReady`。

共享通道不可写或 Ready 为 false 时，发送函数返回 false。原有
`WiepTlmFifo` 不删除队头 Packet，周期事件会在后续拍继续尝试。

## 5. 数据接收

Master 使用：

```cpp
EventWrapper<WiepAxiMstMp,
             &WiepAxiMstMp::recvTimingRespMp>
```

每拍检查 B/R 共享通道。Slave 使用：

```cpp
EventWrapper<WiepAxiSlvMp,
             &WiepAxiSlvMp::recvTimingReqMp>
```

每拍检查 AW/W/AR 共享通道。

只有 `wire.visibleTick <= curTick()` 时才处理数据。WirePacket 被恢复为本地
`PacketPtr` 后，继续调用正常版的接收逻辑。只有本地 FIFO 成功接收 Packet
后，才从共享通道删除该 WirePacket。

正常 Port API 要求的 `recvTimingReq(PacketPtr)` 和
`recvTimingResp(PacketPtr)` 虚函数仍然保留，用于维持基类接口和复用原有
处理逻辑。跨进程入口是无参数的周期事件函数。

## 6. Ready 状态流控

Slave 向 Master 发送：

```cpp
struct WiepAxiSlvReadyMpPacket
{
    bool awReady;
    bool wReady;
    bool arReady;
};
```

Master 向 Slave 发送：

```cpp
struct WiepAxiMstReadyMpPacket
{
    bool rReady;
    bool bReady;
};
```

所有状态初始为 true，不发送初始消息。发送端每拍检查本地接收 FIFO：

```text
Master: R FIFO、B FIFO
Slave : AW FIFO、W FIFO、AR FIFO
```

只有 Ready 状态与上一次成功发送的状态不同，才向状态 TX FIFO 写一条
消息。状态通道写满时不会更新 last-sent 状态，下一拍会继续尝试，因此
不会丢失流控变化。

接收方使用状态 RX FIFO。收到状态的当前拍只保存到 pending 变量，并设置：

```cpp
pendingApplyTick = curTick() + clockPeriod();
```

只有本地 Tick 到达该时间，`sendTimingReq()` / `sendTimingResp()` 才使用
新状态。这保证了“本拍收到 Ready，下一拍影响发送判断”。

## 7. 创建一组 Master/Slave

两端必须使用相同：

- `sessionName`
- `interfaceId`
- Master/Slave 对应的两个 process ID

Master 端：

```cpp
WiepMp::WiepAxiMpConfig config;
config.sessionName = "soc_run_001";
config.interfaceName = "soc_mst0_core0_slv0";
config.interfaceId = 100;
config.localProcessId = 0;
config.remoteProcessId = 1;

WiepAxiMstMp<Host> *mst = new WiepAxiMstMp<Host>(
    name, owner, &axiPack, InvalidPortID, true, &config);
```

Slave 端使用相同接口 ID，并交换本地/远端进程：

```cpp
config.sessionName = "soc_run_001";
config.interfaceName = "soc_mst0_core0_slv0";
config.interfaceId = 100;
config.localProcessId = 1;
config.remoteProcessId = 0;

WiepAxiSlvMp<Host> *slv = new WiepAxiSlvMp<Host>(
    name, owner, &axiPack, InvalidPortID, true, &config);
```

`_mpConfig` 为 NULL 时不会创建共享通道，也不会启动 MP 接收事件，此时 MP
发送函数会返回 false。

## 8. 多组接口

一组 Master/Slave 已包含完整 AXI 请求和响应方向。如果 Core 也需要作为
Master 主动访问 SoC，需要使用另一个 Interface ID 创建反向接口：

| Interface ID | Master | Slave |
|---:|---|---|
| 100 | SoC P0 mst0 | Core0 P1 slv0 |
| 101 | Core0 P1 mst0 | SoC P0 slv0 |
| 200 | SoC P0 mst1 | Core1 P2 slv0 |
| 201 | Core1 P2 mst0 | SoC P0 slv1 |

推荐所有进程读取同一份 TOML：

```toml
[[axi_interface]]
id = 100
name = "soc_mst0_core0_slv0"
master_process = 0
slave_process = 1
master_index = 0
slave_index = 0

[[axi_interface]]
id = 101
name = "core0_mst0_soc_slv0"
master_process = 1
slave_process = 0
master_index = 0
slave_index = 0
```

每个进程根据自己的 process ID 过滤配置：

```cpp
for (const AxiInterfaceConfig &interface : interfaces) {
    if (interface.masterProcess == localProcessId)
        createMaster(interface);
    if (interface.slaveProcess == localProcessId)
        createSlave(interface);
}
```

当前 Owner 回调没有携带接口 ID：

```cpp
m_obj->popAwFunc();
m_obj->popRFunc();
```

同一个 Owner 持有多组接口时，后续应将回调扩展为携带 `interfaceId`，或者
为每组接口创建独立 Adapter Owner，否则 Host 无法直接判断是哪组接口触发。

## 9. 启动与同步要求

共享通道创建者由两端较小的 process ID 确定。两个进程应并行启动，较大
process ID 会等待共享内存出现。

当前 Master/Slave 内部 EventWrapper 只负责每拍轮询数据和 Ready 状态，
不负责多进程全局 barrier。后续接入固定周期同步时，每个进程只能有一个
Runtime 进入 `WiepProcessManager::synchronize()`，不能让每个 AXI 接口
分别进入 barrier。

## 10. 当前验证范围

- 公共 Wire/Ready struct 已通过 C++14 trivially-copyable 检查。
- Slave 已统一为 4 空格和 Allman 大括号风格。
- 修改文件已通过 `git diff --check`。
- 当前工作区不是用户实际使用的旧 gem5/WIEP 完整环境，因此尚未完成
  `WiepAxiMstMp` / `WiepAxiSlvMp` 的真实工程编译和多进程联调。
