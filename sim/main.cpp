#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

#include "xbar_logic_model.hh"

namespace
{

using gem5::xbarlogic::BankGrant;
using gem5::xbarlogic::Command;
using gem5::xbarlogic::NetworkGrant;
using gem5::xbarlogic::NetworkStall;
using gem5::xbarlogic::Request;
using gem5::xbarlogic::TickResult;
using gem5::xbarlogic::XbarConfig;
using gem5::xbarlogic::XbarLogicModel;

std::uint64_t
makeAddress(std::uint32_t logicalBank, std::uint32_t subBank, std::uint32_t word)
{
    return ((static_cast<std::uint64_t>(subBank) & 0x1) << 10)
         | ((static_cast<std::uint64_t>(logicalBank) & 0xf) << 6)
         | ((static_cast<std::uint64_t>(word) & 0xf) << 2);
}

void
printRequest(const Request &request)
{
    std::cout << "id=" << request.id
              << " port=" << request.inputPort
              << " cmd=" << gem5::xbarlogic::commandName(request.command)
              << " qos=" << request.qos
              << " addr=" << gem5::xbarlogic::formatAddress(request.address);
}

void
printCycle(const TickResult &result)
{
    std::cout << "\ncycle " << result.cycle << "\n";

    for (const NetworkGrant &grant : result.arrivedPostXbar) {
        std::cout << "  postq  ";
        printRequest(grant.request);
        std::cout << " -> psubbank=" << grant.decoded.physicalSubBank << "\n";
    }

    for (const BankGrant &grant : result.bankGrants) {
        std::cout << "  bank   ";
        printRequest(grant.request);
        std::cout << " -> grant psubbank=" << grant.decoded.physicalSubBank
                  << "\n";
    }

    for (const auto &stall : result.bankStalls) {
        std::cout << "  bstall ";
        printRequest(stall.request);
        std::cout << " -> " << stall.reason << "\n";
    }

    for (const NetworkGrant &grant : result.acceptedByXbar) {
        std::cout << "  xbar   ";
        printRequest(grant.request);
        std::cout << " -> accepted, arrives next cycle at psubbank="
                  << grant.decoded.physicalSubBank << "\n";
    }

    for (const NetworkStall &stall : result.inputStalls) {
        std::cout << "  istall ";
        printRequest(stall.request);
        std::cout << " -> " << stall.reason << "\n";
    }
}

} // namespace

int
main()
{
    try {
        XbarConfig config;
        config.postXbarQueueDepth = 2;
        config.qosBits = 4;
        config.bankPolicyType = gem5::xbarlogic::BankPolicyType::QoSThenRR;

        XbarLogicModel xbar(config);

        const std::vector<Request> requests = {
            {1, 0, Command::Read,  makeAddress(0, 0, 0), 0x0, 1},
            {2, 1, Command::Read,  makeAddress(0, 0, 1), 0x0, 3},
            {3, 2, Command::Write, makeAddress(0, 0, 2), 0xaaaa, 2},
            {4, 3, Command::Write, makeAddress(0, 1, 3), 0xbbbb, 1},
            {5, 4, Command::Read,  makeAddress(4, 0, 4), 0x0, 2},
            {6, 8, Command::Read,  makeAddress(8, 1, 5), 0x0, 2},
            {7, 9, Command::Write, makeAddress(8, 1, 6), 0xcccc, 2},
            {8, 12, Command::Read, makeAddress(12, 0, 7), 0x0, 0},
        };

        std::cout << "XBAR logic model demo\n";
        std::cout << "inputs=" << config.inputPorts
                  << " logical_banks=" << config.logicalBanks
                  << " sub_banks_per_logical=" << config.subBanksPerLogicalBank
                  << " postq_depth=" << config.postXbarQueueDepth
                  << " bank_policy="
                  << gem5::xbarlogic::bankPolicyName(config.bankPolicyType)
                  << "\n";

        for (const Request &request : requests) {
            xbar.enqueueOrThrow(request);
        }

        for (std::size_t steps = 0; !xbar.idle() && steps < 20; ++steps) {
            printCycle(xbar.tick());
        }

        std::cout << "\nfinished at cycle " << xbar.currentCycle() << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
