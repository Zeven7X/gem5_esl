#include <cstdint>
#include <iostream>
#include <string>

#include "messages.hh"
#include "wiep/mp_sync/mp_fifo.hh"
#include "wiep/mp_sync/sync_manager.hh"

using namespace wiep::mp;

int
main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: worker_sim <session>\n";
        return 2;
    }

    try {
        const std::string session = argv[1];
        MpSyncManager manager(session, MpRole::Worker, 1, 0);
        MpFifo<DemoRequest, 4> requestFifo(
            session, "request", "DemoRequest/v1",
            MpDirection::MainToWorker, MpRole::Worker);
        MpFifo<DemoResponse, 4> responseFifo(
            session, "response", "DemoResponse/v1",
            MpDirection::WorkerToMain, MpRole::Worker);
        manager.registerChannel(requestFifo);
        manager.registerChannel(responseFifo);

        std::uint64_t syncId = 0;
        while (manager.waitInterval(syncId)) {
            DemoRequest request{};
            if (requestFifo.nbRead(request)) {
                const DemoResponse response{
                    request.tag, request.value + 1, 0};
                if (!responseFifo.nbWrite(response))
                    throw MpError("response FIFO unexpectedly full");
            }
            manager.finishInterval(syncId);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "worker_sim: " << error.what() << "\n";
        return 1;
    }
}
