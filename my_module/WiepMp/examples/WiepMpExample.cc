#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "WiepProcessManager.hh"
#include "WiepSharedChannel.hh"

struct TestMessage
{
    std::uint64_t id;
    std::uint64_t value;
};

int
main(int argc, char **argv)
{
    if (argc != 4) {
        std::cerr << "usage: wiep_mp_example <process-id> <process-count> "
                     "<session>\n";
        return 2;
    }

    const std::uint32_t processId =
        static_cast<std::uint32_t>(std::strtoul(argv[1], NULL, 10));
    const std::uint32_t processCount =
        static_cast<std::uint32_t>(std::strtoul(argv[2], NULL, 10));
    if (processCount < 2 || processId >= processCount) {
        std::cerr << "example requires at least process 0 and process 1\n";
        return 2;
    }

    WiepMp::ProcessManagerConfig managerConfig;
    managerConfig.sessionName = argv[3];
    managerConfig.processName = processId == 0 ? "SoC" : "Core0";
    managerConfig.processId = processId;
    managerConfig.processCount = processCount;
    managerConfig.syncPeriodTicks = 1000;
    managerConfig.startupTimeoutSeconds = 10;
    managerConfig.barrierTimeoutSeconds = 10;
    managerConfig.debug = true;

    WiepMp::WiepProcessManager manager(managerConfig);
    if (!manager.initialize())
        return 1;

    WiepMp::SharedChannelConfig channelConfig;
    channelConfig.sessionName = managerConfig.sessionName;
    channelConfig.channelId = 100;
    channelConfig.processId = processId;
    channelConfig.processA = 0;
    channelConfig.processB = 1;
    channelConfig.attachTimeoutSeconds = 10;
    channelConfig.debug = true;

    std::unique_ptr<WiepMp::WiepSharedChannel<TestMessage, 4> > channel;
    if (processId <= 1) {
        channel.reset(
            new WiepMp::WiepSharedChannel<TestMessage, 4>(channelConfig));
        if (!channel->initialize()) {
            manager.markFailed(4001, "channel initialization failed");
            return 1;
        }
    }
    if (!manager.markReady() || !manager.waitForSimulationStart())
        return 1;

    std::uint32_t receivedRequests = 0;
    std::uint32_t receivedResponses = 0;
    for (std::uint64_t epoch = 0; epoch < 5; ++epoch) {
        // Port adapters should check incoming channels before running the
        // business events of the new Epoch.
        TestMessage incoming;
        if (processId == 1 && (epoch == 1 || epoch == 3)) {
            const std::uint32_t firstId = epoch == 1 ? 0 : 3;
            const std::uint32_t count = epoch == 1 ? 3 : 2;
            for (std::uint32_t index = 0; index < count; ++index) {
                if (!channel->nbRead(incoming) ||
                    incoming.id != firstId + index ||
                    incoming.value != 40 + firstId + index) {
                    manager.markFailed(4002, "Core received an invalid request");
                    return 1;
                }
                ++receivedRequests;
                const TestMessage response = {
                    incoming.id, incoming.value + 1};
                if (!channel->nbWrite(response)) {
                    manager.markFailed(4003, "Core response channel is full");
                    return 1;
                }
            }
            if (!channel->empty()) {
                manager.markFailed(4007, "Core request ring did not become empty");
                return 1;
            }
        }
        if (processId == 0 && (epoch == 2 || epoch == 4)) {
            const std::uint32_t firstId = epoch == 2 ? 0 : 3;
            const std::uint32_t count = epoch == 2 ? 3 : 2;
            for (std::uint32_t index = 0; index < count; ++index) {
                if (!channel->nbRead(incoming) ||
                    incoming.id != firstId + index ||
                    incoming.value != 41 + firstId + index) {
                    manager.markFailed(4004, "SoC received an invalid response");
                    return 1;
                }
                ++receivedResponses;
            }
        }

        if (processId == 0 && epoch == 0) {
            for (std::uint32_t id = 0; id < 3; ++id) {
                const TestMessage request = {id, 40 + id};
                if (!channel->nbWrite(request)) {
                    manager.markFailed(4005, "SoC request ring filled too early");
                    return 1;
                }
            }
            const TestMessage overflow = {99, 99};
            if (!channel->full() || channel->nbWrite(overflow)) {
                manager.markFailed(4008, "SPSC full detection failed");
                return 1;
            }
        }
        if (processId == 0 && epoch == 2) {
            for (std::uint32_t id = 3; id < 5; ++id) {
                const TestMessage request = {id, 40 + id};
                if (!channel->nbWrite(request)) {
                    manager.markFailed(4009, "SPSC wrap-around write failed");
                    return 1;
                }
            }
        }

        // This stands in for simulate(syncPeriod).
        const std::uint64_t currentTick =
            (epoch + 1) * manager.syncPeriodTicks();
        if (!manager.synchronize(epoch, currentTick))
            return 1;
    }

    if ((processId == 0 && receivedResponses != 5) ||
        (processId == 1 && receivedRequests != 5)) {
        manager.markFailed(4006, "example completion check failed");
        return 1;
    }

    manager.markFinished();
    if (manager.terminateRequested())
        return 1;
    std::cout << "[WiepMp][P" << processId << "] PASS\n";
    return 0;
}
