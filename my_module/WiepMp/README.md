# WiepMp 多进程仿真组件

## 1. 使用目标

本文档是 `my_module/WiepMp` 的唯一设计与使用说明。组件按以下方式接入 gem5/WIEP：

- 只编译一个仿真程序，通过 `--process-id` 选择当前实例的角色。
- P0 是逻辑主进程和同步协调者，P1～PN-1 分别运行局部模型。
- 每个进程都是独立启动的 gem5 实例；`WiepMp` 不负责 `fork()` Worker。
- `WiepAxiMstMp` / `WiepAxiSlvMp` 在实例化时初始化共享数据通道。
- 同一进程内的普通 gem5 Port 按原方式 `bind()`。
- 不同进程的 MP Port 不调用 `bind()`，通过相同的 `sessionName + interfaceId` 配对。

本文中的顶层程序代码是推荐结构的伪代码，需要按实际 gem5/WIEP 类和 Port API 调整。

## 2. 组件结构

### 2.1 控制面

`WiepProcessManager` 负责：

- P0 创建控制共享内存，其他进程等待并 attach。
- process ID 注册和启动 Ready barrier。
- 固定周期 Epoch barrier。
- Tick、Epoch、heartbeat 和进程状态记录。
- 失败传播、全局终止通知和正常结束汇合。

控制面使用 POSIX 共享内存以及 `PTHREAD_PROCESS_SHARED` mutex/condition variable。Linux 下 mutex 使用 robust 属性，以检测持锁进程异常退出。

### 2.2 数据面

`WiepSharedChannel<T, Capacity>` 在两个进程间建立双向 SPSC ring：

```text
Process A ---- ringAB ----> Process B
Process A <--- ringBA ----- Process B
```

共享类型 `T` 必须 trivially-copyable，且不能包含 `PacketPtr`、其他进程本地指针、STL 容器或虚函数对象。`WiepAxiMstMp` 和 `WiepAxiSlvMp` 在其上构造 AXI AW/W/B/AR/R 和 Ready 通道。

## 3. 规划拓扑

以下示例为一个 P0 加四个 Worker，总进程数为 5：

```text
                         P0: SoC/Coordinator
                          /    |    |    \
                       I101  I102  I103  I104
                        /      |    |      \
                      P1      P2    P3      P4
                   Worker1 Worker2 Worker3 Worker4
```

P0 是逻辑协调者，不要求它一定是 Worker 的操作系统父进程。不要在 gem5 对象、线程或事件队列初始化后直接 `fork()`；应由外部 launcher 启动同一程序的多个实例。如果必须 `fork()`，应在 gem5 初始化前执行，并在子进程中立即 `exec()`。

## 4. Session

参数：

```bash
--session run_001
```

表示本次多进程仿真的唯一会话名，是共享内存的命名空间。例如：

```text
/wiep_mp_ctrl_run_001
/wiep_mp_channel_run_001_808
```

必须遵守：

1. 同一次仿真的所有进程使用相同 session。
2. 同时运行的不同仿真使用不同 session，避免串线。
3. session 由 launcher 生成一次，再原样传给所有进程；各进程不能自行生成。
4. 异常退出可能遗留共享内存，重新运行时建议使用新 session。

推荐：

```bash
SESSION="run_$(date +%Y%m%d_%H%M%S)_$$"
```

## 5. 单一可执行程序

系统只编译一个 `wiep_sim`，启动五次：

```bash
./wiep_sim --process-id 0 --process-count 5 --session run_001
./wiep_sim --process-id 1 --process-count 5 --session run_001
./wiep_sim --process-id 2 --process-count 5 --session run_001
./wiep_sim --process-id 3 --process-count 5 --session run_001
./wiep_sim --process-id 4 --process-count 5 --session run_001
```

| ID | 角色 | 职责 |
|---:|---|---|
| 0 | SoC/Coordinator | 创建控制区、等待所有进程、提交 Epoch |
| 1 | Worker 1 | 创建局部模型并连接 Interface 101 |
| 2 | Worker 2 | 创建局部模型并连接 Interface 102 |
| 3 | Worker 3 | 创建局部模型并连接 Interface 103 |
| 4 | Worker 4 | 创建局部模型并连接 Interface 104 |

## 6. 初始化顺序

所有进程执行相同流程：

```text
解析命令行和公共配置
  -> 创建 WiepProcessManager
  -> manager.initialize()
  -> 根据 processId 创建本进程模型
  -> 实例化本进程 WiepAxiMstMp/WiepAxiSlvMp
  -> MP Port 构造函数初始化共享通道
  -> bind 本进程内仍需连接的普通 Port
  -> manager.markReady()
  -> manager.waitForSimulationStart()
  -> 本地 gem5 仿真与 Epoch 同步
```

约束：

- `manager.initialize()` 早于 MP Port 初始化。
- 本地对象、Port 和共享通道全部成功后才能 `markReady()`。
- `markReady()` 后不能再创建启动所必需的 MP 通道。
- P0 等全部进程 Ready 后切换全局状态为 Running。

## 7. 命令行解析伪代码

```cpp
struct LaunchArgs
{
    std::uint32_t processId = UINT32_MAX;
    std::uint32_t processCount = 0;
    std::uint64_t syncPeriodTicks = 1000;
    std::string sessionName;
    std::string configFile;
};

LaunchArgs
parseArgs(int argc, char **argv)
{
    LaunchArgs args;

    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];

        if (option == "--process-id" && i + 1 < argc) {
            args.processId = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (option == "--process-count" && i + 1 < argc) {
            args.processCount = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (option == "--session" && i + 1 < argc) {
            args.sessionName = argv[++i];
        } else if (option == "--config" && i + 1 < argc) {
            args.configFile = argv[++i];
        } else if (option == "--sync-period" && i + 1 < argc) {
            args.syncPeriodTicks = static_cast<std::uint64_t>(std::stoull(argv[++i]));
        } else {
            throw std::runtime_error("unknown or incomplete option: " + option);
        }
    }

    if (args.processId == UINT32_MAX)
        throw std::runtime_error("--process-id is required");
    if (args.processCount == 0)
        throw std::runtime_error("--process-count is required");
    if (args.processId >= args.processCount)
        throw std::runtime_error("--process-id is out of range");
    if (args.sessionName.empty())
        throw std::runtime_error("--session is required");

    return args;
}
```

必须在构造 `WiepProcessManager` 和 gem5 平台对象前得到本地 process ID。

## 8. 进程角色接口

```cpp
class ProcessRole
{
  public:
    virtual ~ProcessRole() {}

    // 创建本地模型、MP Port、共享通道和本地连接。
    virtual bool initialize() = 0;

    // barrier 返回后处理新 Epoch 可见的输入。
    virtual void beginEpoch(std::uint64_t epoch) = 0;

    // 将本地 gem5 事件队列运行到绝对目标 Tick。
    virtual bool simulateUntil(Tick targetTick) = 0;

    // barrier 前发布本 Epoch 输出。
    virtual bool endEpoch(std::uint64_t epoch) = 0;

    virtual bool simulationFinished() const = 0;
};
```

推荐一个 `SocProcessRole` 和一个带 `workerId` 的 `WorkerProcessRole`。如果四个 Worker 模型不同，可以分别实现四个派生类。

## 9. `main()` 完整伪代码

```cpp
int
main(int argc, char **argv)
{
    LaunchArgs args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "argument error: " << error.what() << std::endl;
        return 2;
    }

    if (args.processCount != 5) {
        std::cerr << "this topology requires 5 processes" << std::endl;
        return 2;
    }

    WiepMp::ProcessManagerConfig managerConfig;
    managerConfig.sessionName = args.sessionName;
    managerConfig.processId = args.processId;
    managerConfig.processCount = args.processCount;
    managerConfig.syncPeriodTicks = args.syncPeriodTicks;
    managerConfig.startupTimeoutSeconds = 60;
    managerConfig.barrierTimeoutSeconds = 60;
    managerConfig.unlinkOnExit = true;
    managerConfig.debug = true;
    managerConfig.processName = args.processId == 0
        ? "soc" : "worker_" + std::to_string(args.processId);

    WiepMp::WiepProcessManager processManager(managerConfig);
    if (!processManager.initialize())
        return 1;

    std::unique_ptr<ProcessRole> localRole;
    try {
        switch (args.processId) {
          case 0:
            localRole.reset(new SocProcessRole(args));
            break;
          case 1:
          case 2:
          case 3:
          case 4:
            localRole.reset(new WorkerProcessRole(args, args.processId));
            break;
          default:
            processManager.markFailed(1001, "unsupported process ID");
            return 1;
        }

        // initialize() 内部：
        // 1. 创建本进程 gem5/WIEP 模型；
        // 2. 实例化 WiepAxiMstMp/WiepAxiSlvMp；
        // 3. MP Port 构造时初始化共享通道；
        // 4. bind 本进程内仍需连接的普通 gem5 Port。
        if (!localRole->initialize()) {
            processManager.markFailed(1002, "local model initialization failed");
            return 1;
        }
    } catch (const std::exception &error) {
        processManager.markFailed(
            1003, std::string("initialization exception: ") + error.what());
        return 1;
    }

    if (!processManager.markReady()) {
        processManager.markFailed(1004, "markReady failed");
        return 1;
    }
    if (!processManager.waitForSimulationStart())
        return 1;

    std::uint64_t epoch = 0;
    while (!localRole->simulationFinished()) {
        if (processManager.terminateRequested())
            return 1;

        localRole->beginEpoch(epoch);
        const Tick targetTick =
            (epoch + 1) * processManager.syncPeriodTicks();

        if (!localRole->simulateUntil(targetTick)) {
            processManager.markFailed(2001, "local simulation failed");
            return 1;
        }
        if (!localRole->endEpoch(epoch)) {
            processManager.markFailed(2002, "failed to publish Epoch data");
            return 1;
        }
        if (!processManager.synchronize(epoch, curTick()))
            return 1;

        ++epoch;
    }

    processManager.markFinished();
    return processManager.terminateRequested() ? 1 : 0;
}
```

当前 `synchronize()` 要求：

```cpp
currentTick == (epoch + 1) * syncPeriodTicks
```

所以 `simulateUntil()` 应运行到绝对目标 Tick。如果 gem5 被其他 ExitEvent 提前唤醒，必须检查原因，不能用错误 Tick 进入 barrier。

## 10. AXI MP 配置

```cpp
WiepMp::WiepAxiMpConfig
makeMpConfig(const LaunchArgs &args,
             std::uint32_t interfaceId,
             std::uint32_t remoteProcessId,
             const std::string &interfaceName)
{
    WiepMp::WiepAxiMpConfig config;
    config.sessionName = args.sessionName;
    config.interfaceName = interfaceName;
    config.interfaceId = interfaceId;
    config.localProcessId = args.processId;
    config.remoteProcessId = remoteProcessId;
    config.attachTimeoutSeconds = 60;
    config.unlinkOnExit = true;
    config.debug = true;
    return config;
}
```

例如 P0/P4 两端：

```text
P0 Master: session=run_001, interfaceId=104, local=0, remote=4
P4 Slave : session=run_001, interfaceId=104, local=4, remote=0
```

两端的 session、interface ID、WirePacket ABI 和容量必须一致，本地/远端 ID 互换。

一个 AXI 接口映射为 8 个连续 channel ID：

```cpp
channelId = interfaceId * 8 + channelOffset;
```

| Offset | 通道 | 方向 |
|---:|---|---|
| 0 | AW | Master → Slave |
| 1 | W | Master → Slave |
| 2 | B | Slave → Master |
| 3 | AR | Master → Slave |
| 4 | R | Slave → Master |
| 5 | Master Ready | Master → Slave |
| 6 | Slave Ready | Slave → Master |
| 7 | 保留 | - |

## 11. P0 初始化伪代码

```cpp
bool
SocProcessRole::initialize()
{
    soc.reset(new Soc(/* params */));
    cpu.reset(new Cpu(/* params */));
    cache.reset(new Cache(/* params */));
    xbar.reset(new XBar(/* params */));

    for (std::uint32_t workerId = 1; workerId <= 4; ++workerId) {
        const std::uint32_t interfaceId = 100 + workerId;
        WiepMp::WiepAxiMpConfig mpConfig = makeMpConfig(
            args, interfaceId, workerId,
            "soc_worker_" + std::to_string(workerId));

        // 构造函数内部初始化 AW/W/B/AR/R/Ready 共享通道。
        mpMasters.emplace_back(new WiepAxiMstMp<Soc>(
            makePortName(workerId), soc.get(), &axiPack,
            InvalidPortID, true, &mpConfig));
    }

    bindLocalComponents();
    initializeLocalGem5Objects();
    return true;
}
```

## 12. Worker 初始化伪代码

```cpp
bool
WorkerProcessRole::initialize()
{
    worker.reset(new Worker(workerId, /* params */));
    localCache.reset(new Cache(/* params */));
    localXbar.reset(new XBar(/* params */));

    const std::uint32_t interfaceId = 100 + workerId;
    WiepMp::WiepAxiMpConfig mpConfig = makeMpConfig(
        args, interfaceId, 0,
        "soc_worker_" + std::to_string(workerId));

    mpSlave.reset(new WiepAxiSlvMp<Worker>(
        "soc_slv", worker.get(), &axiPack,
        InvalidPortID, true, &mpConfig));

    bindLocalComponents();
    initializeLocalGem5Objects();
    return true;
}
```

如果 Worker 也要作为 Master 主动访问 SoC，应创建一组新的反向接口并使用不同 `interfaceId`，不能复用 P0 Master → Worker Slave 接口。

## 13. 本地 bind 与跨进程连接

### 13.1 Port 与 Owner

构造参数 `_owner` 用于确定 Port 所属 `MemObject`、调用业务回调、取得时钟周期和调度 Event。例如：

```cpp
m_obj(dynamic_cast<C_OWNER *>(_owner));
m_obj->clockPeriod();
```

这不是 Master/Slave bind。

### 13.2 同进程普通 Port

只有同一地址空间内的普通 gem5 Port 才 `bind()`：

```cpp
cpu->memPort.bind(cache->cpuSidePort);
cache->memSidePort.bind(xbar->cpuSidePort);
localXbar->memSidePort.bind(localMemory->port);
```

如果 `WiepAxiMstMp/WiepAxiSlvMp` 本身就是 Owner 直接持有和调用的外部 AXI Port，不存在单独的本地 Adapter 侧，那么创建它并传入 Owner 后可能不需要额外本地 bind。

### 13.3 跨进程 MP Port

不同进程的对象不能用 C++ 指针绑定：

```cpp
p0Master->bind(*p1Slave); // 错误
```

跨进程关系只依赖：

```text
相同 sessionName
+ 相同 interfaceId
+ 正确 localProcessId/remoteProcessId
+ 相同协议与 WirePacket ABI
```

准确的初始化注释是：

```cpp
// 1. 创建本进程内部的 gem5/WIEP 模型；
// 2. 实例化本进程的 WiepAxiMstMp/WiepAxiSlvMp；
// 3. MP Port 构造函数内部初始化共享数据通道；
// 4. bind 本进程内部仍需连接的普通 gem5 Port（如果存在）；
// 5. 不 bind 跨进程 Master/Slave；跨进程关系由
//    sessionName + interfaceId 建立。
```

## 14. Epoch 同步与数据可见性

每个进程只能有一个 Runtime/顶层控制点调用全局 `synchronize()`，不能让每个 AXI Port 分别进入 barrier。

```text
进入 Epoch N
  -> 读取当前 Epoch 可见输入
  -> 运行本同步周期的本地事件
  -> 编码并发布输出
  -> synchronize(N, currentTick)
  -> 等待全部进程
  -> 进入 Epoch N+1
```

长期目标应使用 Epoch 双缓冲，使 Epoch N 的输出只在 N+1 对接收方可见。当前 `WiepSharedChannel` 是立即可见 SPSC ring，AXI 骨架使用 `visibleTick` 延迟处理；两者不完全等价，正式确定性验证前需要统一方案。

## 15. Ready 与 Packet

Ready 状态规划：

- Slave 向 Master 发送 AW/W/AR Ready。
- Master 向 Slave 发送 R/B Ready。
- 仅在状态变化时发送；状态 FIFO 满时下一拍重试。
- 收到的状态先保存为 pending，下一拍生效。
- 通道不可写或远端 Ready 为 false 时发送返回 `false`。

`PacketPtr` 只能在创建它的进程中使用。当前 `packetToAwMp()`、`packetToWMp()`、`packetToBMp()`、`packetToArMp()`、`packetToRMp()` 及反向函数仍为占位实现。正式运行前必须补齐地址、tag、size、burst、QoS、response、W/R payload、strobe、last、本地 Packet 生命周期和 OSTD/顺序检查。

## 16. 启动脚本

```bash
#!/bin/bash

set -u
SESSION="run_$(date +%Y%m%d_%H%M%S)_$$"

for ID in 0 1 2 3 4; do
    ./wiep_sim \
        --process-id "${ID}" \
        --process-count 5 \
        --session "${SESSION}" \
        --config system.toml &
done

wait
```

必须并行启动所有实例。P0 创建控制区；Worker 等待 attach。每个 Channel 由两端 process ID 较小者创建，另一端等待 attach。

## 17. 退出与错误

正常结束：

```cpp
processManager.markFinished();
```

任一错误必须传播：

```cpp
processManager.markFailed(errorCode, "failure reason");
return 1;
```

其他进程在循环中检查 `terminateRequested()`。当前星形拓扑中 P0 是全部通道的较小 ID 和 owner；未来支持 Worker 互连时，应将共享对象 unlink 改为 session coordinator 统一清理。

## 18. 构建与示例

目标环境为 Linux/CentOS 7、GCC 7.3～13.1、C++14、POSIX shared memory、pthread 和 librt。

```bash
cmake -S my_module/WiepMp -B build/WiepMp
cmake --build build/WiepMp -j
```

独立测试目前使用位置参数：

```bash
./build/WiepMp/wiep_mp_example 0 2 example_session &
./build/WiepMp/wiep_mp_example 1 2 example_session
wait
```

该示例验证控制面和通用共享 Channel，不代表真实 AXI Packet 编解码已经完成。

## 19. 当前状态与后续工作

当前可继续使用：

- 控制区创建/attach、进程注册、Ready/Epoch barrier。
- process-shared pthread 同步。
- trivially-copyable SPSC 共享 ring。
- AXI 五通道、Ready 通道和 channel ID 命名结构。

真实接入前必须完成：

1. 实现全部 `PacketPtr` ↔ WirePacket 转换。
2. 对规范化公共配置计算哈希，并在 attach 时校验完整拓扑。
3. 统一 Epoch 双缓冲与 `visibleTick + SPSC` 的数据可见性协议。
4. 给 MP Port 增加显式初始化错误返回；构造函数内 `WIEP_ASSERT` 不利于优雅失败。
5. 按实际 `WiepMasterPort/WiepSlavePort` 确认本地 bind API。
6. 多接口 Owner 的业务回调携带 `interfaceId`，或每组接口使用独立 Adapter Owner。
7. 增加 P0 + 四 Worker 的启动、同步、异常退出和数据传输测试。

## 20. 接入检查清单

- [ ] 所有进程的 session、process count 和同步周期一致。
- [ ] 每个 process ID 唯一且有效。
- [ ] 每个 interface ID 全局唯一，两端 ID 配置互换。
- [ ] 两端 WirePacket ABI 和容量一致。
- [ ] 先初始化 ProcessManager，再创建本地模型和 MP Port。
- [ ] MP Port/通道全部成功后才 `markReady()`。
- [ ] 只 bind 同一进程内的普通 Port。
- [ ] 不 bind 跨进程 MP 对端。
- [ ] 每进程每 Epoch 只调用一次 `synchronize()`。
- [ ] barrier Tick 等于绝对目标 Tick。
- [ ] 共享内存中不存在进程本地指针。
- [ ] 失败调用 `markFailed()`，正常结束调用 `markFinished()`。
