#include "xbar_logic_model.hh"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gem5::xbarlogic
{

namespace
{

constexpr std::uint64_t OffsetMask = 0x3f;
constexpr std::uint64_t LogicalBankMask = 0xf;
constexpr std::uint64_t SubBankMask = 0x1;

std::string
describeTarget(const DecodedAddress &decoded)
{
    std::ostringstream out;
    out << "logical_bank=" << decoded.logicalBank
        << " sub_bank=" << decoded.subBank
        << " physical_sub_bank=" << decoded.physicalSubBank;
    return out.str();
}

class QoSThenRRBankPolicy : public BankArbitrationPolicy
{
  public:
    std::optional<std::size_t>
    choose(const std::vector<BankCandidate> &candidates,
           std::size_t physicalSubBank,
           std::uint64_t) override
    {
        if (candidates.empty()) {
            return std::nullopt;
        }

        if (nextCommand.size() <= physicalSubBank) {
            nextCommand.resize(physicalSubBank + 1, Command::Read);
        }

        std::uint32_t bestQos = 0;
        for (const BankCandidate &candidate : candidates) {
            bestQos = std::max(bestQos, candidate.request->qos);
        }

        std::vector<std::size_t> tied;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].request->qos == bestQos) {
                tied.push_back(i);
            }
        }

        const Command preferred = nextCommand[physicalSubBank];
        std::size_t chosen = tied.front();
        for (std::size_t index : tied) {
            if (candidates[index].command == preferred) {
                chosen = index;
                break;
            }
        }

        nextCommand[physicalSubBank] =
            candidates[chosen].command == Command::Read ? Command::Write
                                                        : Command::Read;
        return chosen;
    }

    std::string name() const override { return "QoSThenRR"; }

  private:
    std::vector<Command> nextCommand;
};

class ReadPriorityBankPolicy : public BankArbitrationPolicy
{
  public:
    std::optional<std::size_t>
    choose(const std::vector<BankCandidate> &candidates,
           std::size_t,
           std::uint64_t) override
    {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].command == Command::Read) {
                return i;
            }
        }

        return candidates.empty() ? std::nullopt
                                  : std::optional<std::size_t>(0);
    }

    std::string name() const override { return "ReadPriority"; }
};

class WritePriorityBankPolicy : public BankArbitrationPolicy
{
  public:
    std::optional<std::size_t>
    choose(const std::vector<BankCandidate> &candidates,
           std::size_t,
           std::uint64_t) override
    {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].command == Command::Write) {
                return i;
            }
        }

        return candidates.empty() ? std::nullopt
                                  : std::optional<std::size_t>(0);
    }

    std::string name() const override { return "WritePriority"; }
};

class SevenReadsOneWriteBankPolicy : public BankArbitrationPolicy
{
  public:
    std::optional<std::size_t>
    choose(const std::vector<BankCandidate> &candidates,
           std::size_t physicalSubBank,
           std::uint64_t) override
    {
        if (candidates.empty()) {
            return std::nullopt;
        }

        if (readCredits.size() <= physicalSubBank) {
            readCredits.resize(physicalSubBank + 1, 0);
        }

        std::optional<std::size_t> readIndex;
        std::optional<std::size_t> writeIndex;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].command == Command::Read) {
                readIndex = i;
            } else {
                writeIndex = i;
            }
        }

        if (readIndex && (!writeIndex || readCredits[physicalSubBank] < 7)) {
            ++readCredits[physicalSubBank];
            return readIndex;
        }

        if (writeIndex) {
            readCredits[physicalSubBank] = 0;
            return writeIndex;
        }

        return readIndex;
    }

    std::string name() const override { return "SevenReadsOneWrite"; }

  private:
    std::vector<std::uint8_t> readCredits;
};

} // namespace

std::shared_ptr<BankArbitrationPolicy>
makeBankArbitrationPolicy(BankPolicyType type)
{
    switch (type) {
      case BankPolicyType::QoSThenRR:
        return std::make_shared<QoSThenRRBankPolicy>();
      case BankPolicyType::ReadPriority:
        return std::make_shared<ReadPriorityBankPolicy>();
      case BankPolicyType::WritePriority:
        return std::make_shared<WritePriorityBankPolicy>();
      case BankPolicyType::SevenReadsOneWrite:
        return std::make_shared<SevenReadsOneWriteBankPolicy>();
    }

    throw std::invalid_argument("unknown bank arbitration policy");
}

XbarLogicModel::XbarLogicModel(
    XbarConfig config,
    std::shared_ptr<BankArbitrationPolicy> bankPolicy)
    : config(std::move(config)),
      readInputs(this->config.inputPorts),
      writeInputs(this->config.inputPorts),
      pendingByPhysicalSubBank(
          this->config.logicalBanks * this->config.subBanksPerLogicalBank),
      readStage1Rr(this->config.inputPorts),
      readStage2Rr(this->config.inputPorts),
      readStage3Rr(this->config.logicalBanks),
      writeStage1Rr(this->config.inputPorts),
      writeStage2Rr(this->config.inputPorts),
      writeStage3Rr(this->config.logicalBanks),
      bankPolicy(bankPolicy ? std::move(bankPolicy)
                            : makeBankArbitrationPolicy(
                                  this->config.bankPolicyType))
{
    validateConfig();
}

bool
XbarLogicModel::tryEnqueue(const Request &request)
{
    validateRequest(request);

    std::deque<Request> &queue = inputQueueFor(request);
    if (queue.size() >= config.inputQueueDepth) {
        return false;
    }

    queue.push_back(request);
    return true;
}

void
XbarLogicModel::enqueueOrThrow(const Request &request)
{
    if (!tryEnqueue(request)) {
        throw std::runtime_error("input queue is full for port " +
                                 std::to_string(request.inputPort));
    }
}

TickResult
XbarLogicModel::tick()
{
    TickResult result;
    result.cycle = cycle;

    deliverNetworkArrivals(result);
    arbitrateBanks(result);
    arbitrateNetworkPath(
        Command::Read, readStage1Rr, readStage2Rr, readStage3Rr, result);
    arbitrateNetworkPath(
        Command::Write, writeStage1Rr, writeStage2Rr, writeStage3Rr, result);

    ++cycle;
    return result;
}

bool
XbarLogicModel::idle() const
{
    if (!oneCycleNetworkPipe.empty()) {
        return false;
    }

    for (std::size_t port = 0; port < config.inputPorts; ++port) {
        if (!readInputs[port].empty() || !writeInputs[port].empty()) {
            return false;
        }
    }

    for (const PendingQueues &queues : pendingByPhysicalSubBank) {
        if (!queues.reads.empty() || !queues.writes.empty()) {
            return false;
        }
    }

    return true;
}

const XbarConfig &
XbarLogicModel::params() const
{
    return config;
}

std::uint64_t
XbarLogicModel::currentCycle() const
{
    return cycle;
}

DecodedAddress
XbarLogicModel::decodeAddress(std::uint64_t address) const
{
    DecodedAddress decoded;
    decoded.offset = static_cast<std::uint32_t>(address & OffsetMask);
    decoded.logicalBank = static_cast<std::size_t>(
        (address >> 6) & LogicalBankMask);
    decoded.subBank = static_cast<std::size_t>((address >> 10) & SubBankMask);
    decoded.physicalSubBank =
        decoded.logicalBank * config.subBanksPerLogicalBank + decoded.subBank;

    if (decoded.logicalBank >= config.logicalBanks) {
        throw std::out_of_range("logical bank decoded outside configured range");
    }

    if (decoded.subBank >= config.subBanksPerLogicalBank) {
        throw std::out_of_range("sub-bank decoded outside configured range");
    }

    return decoded;
}

std::size_t
XbarLogicModel::inputOccupancy(std::size_t inputPort, Command command) const
{
    return inputQueueFor(inputPort, command).size();
}

std::size_t
XbarLogicModel::pendingOccupancy(
    std::size_t physicalSubBank,
    Command command) const
{
    return pendingQueueFor(physicalSubBank, command).size();
}

void
XbarLogicModel::validateConfig() const
{
    if (config.inputPorts != 16) {
        throw std::invalid_argument(
            "this fixed 4x4 + 2x2 + 2x2 fabric currently expects 16 inputs");
    }

    if (config.logicalBanks != 16) {
        throw std::invalid_argument(
            "addr[9:6] selects 16 logical banks, so logicalBanks must be 16");
    }

    if (config.subBanksPerLogicalBank != 2) {
        throw std::invalid_argument(
            "addr[10] selects 2 physical sub-banks per logical bank");
    }

    if (config.postXbarQueueDepth == 0 || config.inputQueueDepth == 0) {
        throw std::invalid_argument("queue depths must be non-zero");
    }

    if (config.qosBits == 0 || config.qosBits > 31) {
        throw std::invalid_argument("qosBits must be in the range [1, 31]");
    }
}

void
XbarLogicModel::validateRequest(const Request &request) const
{
    if (request.inputPort >= config.inputPorts) {
        throw std::out_of_range("request input port is outside the XBAR");
    }

    const std::uint32_t maxQos = (1u << config.qosBits) - 1u;
    if (request.qos > maxQos) {
        throw std::out_of_range("request QoS exceeds configured qosBits");
    }

    (void)decodeAddress(request.address);
}

std::deque<Request> &
XbarLogicModel::inputQueueFor(const Request &request)
{
    return request.command == Command::Read ? readInputs[request.inputPort]
                                            : writeInputs[request.inputPort];
}

const std::deque<Request> &
XbarLogicModel::inputQueueFor(
    std::size_t inputPort,
    Command command) const
{
    if (inputPort >= config.inputPorts) {
        throw std::out_of_range("input port is outside the XBAR");
    }

    return command == Command::Read ? readInputs[inputPort]
                                    : writeInputs[inputPort];
}

void
XbarLogicModel::deliverNetworkArrivals(TickResult &result)
{
    result.arrivedPostXbar = oneCycleNetworkPipe;

    for (const NetworkGrant &grant : oneCycleNetworkPipe) {
        pendingQueueFor(grant.decoded.physicalSubBank,
                        grant.request.command).push_back(grant.request);
    }

    oneCycleNetworkPipe.clear();
}

void
XbarLogicModel::arbitrateBanks(TickResult &result)
{
    for (std::size_t physical = 0;
         physical < pendingByPhysicalSubBank.size();
         ++physical) {
        std::vector<BankCandidate> candidates;
        if (!pendingByPhysicalSubBank[physical].reads.empty()) {
            candidates.push_back({
                &pendingByPhysicalSubBank[physical].reads.front(),
                decodeAddress(pendingByPhysicalSubBank[physical]
                                  .reads.front()
                                  .address),
                Command::Read,
            });
        }

        if (!pendingByPhysicalSubBank[physical].writes.empty()) {
            candidates.push_back({
                &pendingByPhysicalSubBank[physical].writes.front(),
                decodeAddress(pendingByPhysicalSubBank[physical]
                                  .writes.front()
                                  .address),
                Command::Write,
            });
        }

        if (candidates.empty()) {
            continue;
        }

        const std::optional<std::size_t> chosen =
            bankPolicy->choose(candidates, physical, cycle);
        if (!chosen) {
            for (const BankCandidate &candidate : candidates) {
                result.bankStalls.push_back({
                    *candidate.request,
                    candidate.decoded,
                    "bank policy did not grant this cycle",
                });
            }
            continue;
        }

        const BankCandidate &winner = candidates[*chosen];
        result.bankGrants.push_back({*winner.request, winner.decoded});

        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (i == *chosen) {
                continue;
            }

            result.bankStalls.push_back({
                *candidates[i].request,
                candidates[i].decoded,
                "lost bank read/write arbitration at physical sub-bank " +
                    std::to_string(physical),
            });
        }

        pendingQueueFor(physical, winner.command).pop_front();
    }
}

void
XbarLogicModel::arbitrateNetworkPath(
    Command command,
    RrState &stage1Rr,
    RrState &stage2Rr,
    RrState &stage3Rr,
    TickResult &result)
{
    std::vector<NetworkCandidate> candidates;
    for (std::size_t port = 0; port < config.inputPorts; ++port) {
        const std::deque<Request> &queue = inputQueueFor(port, command);
        if (queue.empty()) {
            continue;
        }

        const Request &request = queue.front();
        const DecodedAddress decoded = decodeAddress(request.address);
        if (!postQueueCanAccept(decoded, command)) {
            result.inputStalls.push_back({
                request,
                "post-XBAR queue full for " + describeTarget(decoded),
            });
            continue;
        }

        candidates.push_back({request, decoded});
    }

    candidates = arbitrateStage(
        candidates, stage1Rr, config.inputPorts, "stage1_4x4",
        stage1Key, result);
    candidates = arbitrateStage(
        candidates, stage2Rr, config.inputPorts, "stage2_2x2",
        stage2Key, result);
    candidates = arbitrateStage(
        candidates, stage3Rr, config.logicalBanks, "stage3_2x2",
        stage3Key, result);

    for (const NetworkCandidate &candidate : candidates) {
        result.acceptedByXbar.push_back({
            candidate.request,
            candidate.decoded,
        });
        oneCycleNetworkPipe.push_back({
            candidate.request,
            candidate.decoded,
        });

        inputQueueFor(candidate.request).pop_front();
    }
}

std::vector<XbarLogicModel::NetworkCandidate>
XbarLogicModel::arbitrateStage(
    const std::vector<NetworkCandidate> &candidates,
    RrState &rrState,
    std::size_t outputCount,
    const std::string &stageName,
    std::size_t (*keyFn)(const NetworkCandidate &),
    TickResult &result) const
{
    std::vector<std::vector<std::size_t>> byOutput(outputCount);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const std::size_t key = keyFn(candidates[i]);
        if (key >= outputCount) {
            throw std::runtime_error(stageName + " output key is out of range");
        }

        byOutput[key].push_back(i);
    }

    std::vector<NetworkCandidate> winners;
    for (std::size_t output = 0; output < outputCount; ++output) {
        if (byOutput[output].empty()) {
            continue;
        }

        const std::optional<std::size_t> chosen =
            chooseQoSThenRR(candidates, byOutput[output], rrState, output);
        if (!chosen) {
            continue;
        }

        winners.push_back(candidates[*chosen]);
        for (std::size_t candidateIndex : byOutput[output]) {
            if (candidateIndex == *chosen) {
                continue;
            }

            result.inputStalls.push_back({
                candidates[candidateIndex].request,
                "lost " + stageName + " output " +
                    std::to_string(output) + " arbitration",
            });
        }
    }

    return winners;
}

std::optional<std::size_t>
XbarLogicModel::chooseQoSThenRR(
    const std::vector<NetworkCandidate> &candidates,
    const std::vector<std::size_t> &candidateIndexes,
    RrState &rrState,
    std::size_t outputKey) const
{
    if (candidateIndexes.empty()) {
        return std::nullopt;
    }

    std::uint32_t bestQos = 0;
    for (std::size_t index : candidateIndexes) {
        bestQos = std::max(bestQos, candidates[index].request.qos);
    }

    std::optional<std::size_t> chosen;
    std::size_t bestDistance = std::numeric_limits<std::size_t>::max();
    const std::size_t rrStart = rrState.next[outputKey] % config.inputPorts;

    for (std::size_t index : candidateIndexes) {
        const Request &request = candidates[index].request;
        if (request.qos != bestQos) {
            continue;
        }

        const std::size_t distance =
            (request.inputPort + config.inputPorts - rrStart) %
            config.inputPorts;
        if (distance < bestDistance) {
            bestDistance = distance;
            chosen = index;
        }
    }

    if (chosen) {
        rrState.next[outputKey] =
            (candidates[*chosen].request.inputPort + 1) % config.inputPorts;
    }

    return chosen;
}

std::size_t
XbarLogicModel::stage1Key(const NetworkCandidate &candidate)
{
    const std::size_t inputGroup = candidate.request.inputPort / 4;
    const std::size_t bankLowBits = candidate.decoded.logicalBank & 0x3;
    return inputGroup * 4 + bankLowBits;
}

std::size_t
XbarLogicModel::stage2Key(const NetworkCandidate &candidate)
{
    const std::size_t inputGroup = candidate.request.inputPort / 4;
    const std::size_t inputPair = inputGroup / 2;
    const std::size_t bankLowBits = candidate.decoded.logicalBank & 0x3;
    const std::size_t bankBit2 = (candidate.decoded.logicalBank >> 2) & 0x1;
    return bankLowBits * 4 + inputPair * 2 + bankBit2;
}

std::size_t
XbarLogicModel::stage3Key(const NetworkCandidate &candidate)
{
    return candidate.decoded.logicalBank;
}

bool
XbarLogicModel::postQueueCanAccept(
    const DecodedAddress &decoded,
    Command command) const
{
    return pendingQueueFor(decoded.physicalSubBank, command).size() <
           config.postXbarQueueDepth;
}

std::deque<Request> &
XbarLogicModel::pendingQueueFor(
    std::size_t physicalSubBank,
    Command command)
{
    if (physicalSubBank >= pendingByPhysicalSubBank.size()) {
        throw std::out_of_range("physical sub-bank is outside the XBAR");
    }

    return command == Command::Read
        ? pendingByPhysicalSubBank[physicalSubBank].reads
        : pendingByPhysicalSubBank[physicalSubBank].writes;
}

const std::deque<Request> &
XbarLogicModel::pendingQueueFor(
    std::size_t physicalSubBank,
    Command command) const
{
    if (physicalSubBank >= pendingByPhysicalSubBank.size()) {
        throw std::out_of_range("physical sub-bank is outside the XBAR");
    }

    return command == Command::Read
        ? pendingByPhysicalSubBank[physicalSubBank].reads
        : pendingByPhysicalSubBank[physicalSubBank].writes;
}

std::string
commandName(Command command)
{
    return command == Command::Read ? "RD" : "WR";
}

std::string
bankPolicyName(BankPolicyType type)
{
    switch (type) {
      case BankPolicyType::QoSThenRR:
        return "QoSThenRR";
      case BankPolicyType::ReadPriority:
        return "ReadPriority";
      case BankPolicyType::WritePriority:
        return "WritePriority";
      case BankPolicyType::SevenReadsOneWrite:
        return "SevenReadsOneWrite";
    }

    return "Unknown";
}

std::string
formatAddress(std::uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

} // namespace gem5::xbarlogic
