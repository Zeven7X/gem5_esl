#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "messages.hh"
#include "wiep/mp_sync/mp_fifo.hh"
#include "wiep/mp_sync/sync_manager.hh"

using namespace wiep::mp;

int
main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: main_sim <worker_sim_path>\n";
        return 2;
    }

    try {
        const std::string session =
            "demo_" + std::to_string(static_cast<unsigned long>(getpid()));
        MpSyncManager manager(session, MpRole::Main, 1);
        MpFifo<DemoRequest, 4> requestFifo(
            session, "request", "DemoRequest/v1",
            MpDirection::MainToWorker, MpRole::Main);
        MpFifo<DemoResponse, 4> responseFifo(
            session, "response", "DemoResponse/v1",
            MpDirection::WorkerToMain, MpRole::Main);
        manager.registerChannel(requestFifo);
        manager.registerChannel(responseFifo);

        const pid_t worker = fork();
        if (worker < 0) {
            std::cerr << "fork failed, errno=" << errno << "\n";
            return 2;
        }
        if (worker == 0) {
            execl(argv[1], argv[1], session.c_str(), nullptr);
            _exit(127);
        }

        manager.beginInterval(0);
        const DemoRequest request{7, 0x1000, 41, 1};
        if (!requestFifo.nbWrite(request))
            throw MpError("request FIFO unexpectedly full");
        manager.endInterval(0);

        manager.beginInterval(1);
        if (responseFifo.canPop())
            throw MpError("response became visible one interval too early");
        manager.endInterval(1);

        manager.beginInterval(2);
        DemoResponse response{};
        if (!responseFifo.nbRead(response))
            throw MpError("response was not visible at sync 2");
        if (response.tag != request.tag || response.value != 42 ||
            response.status != 0) {
            throw MpError("response contents do not match");
        }
        if (requestFifo.numFree() != 4)
            throw MpError("read credit was not returned at sync 2");
        manager.endInterval(2);
        manager.stop();

        int status = 0;
        waitpid(worker, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw MpError("worker exited with an error");

        std::cout << "PASS: request/response and read credit crossed "
                     "processes at the expected sync points\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "main_sim: " << error.what() << "\n";
        return 1;
    }
}
