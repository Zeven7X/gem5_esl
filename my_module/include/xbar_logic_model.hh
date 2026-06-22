#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gem5::xbarlogic
{

enum class Command
{
    Read,
    Write,
};

enum class BankPolicyType
{
    QoSThenRR,
    ReadPriority,
    WritePriority,
    SevenReadsOneWrite,
};

struct XbarConfig
{
    std::size_t inputPorts = 16;
    std::size_t logicalBanks = 16;
    std::size_t subBanksPerLogicalBank = 2;
    std::size_t postXbarQueueDepth = 4;
    std::size_t inputQueueDepth = 8;
    std::uint8_t qosBits = 4;
    BankPolicyType bankPolicyType = BankPolicyType::QoSThenRR;
};

struct Request
{
    std::uint64_t id = 0;
    std::size_t inputPort = 0;
    Command command = Command::Read;
    std::uint64_t address = 0;
    std::uint64_t data = 0;
    std::uint32_t qos = 0;
};

struct DecodedAddress
{
    std::uint32_t offset = 0;
    std::size_t logicalBank = 0;
    std::size_t subBank = 0;
    std::size_t physicalSubBank = 0;
};

struct NetworkGrant
{
    Request request;
    DecodedAddress decoded;
};

struct NetworkStall
{
    Request request;
    std::string reason;
};

struct BankGrant
{
    Request request;
    DecodedAddress decoded;
};

struct BankStall
{
    Request request;
    DecodedAddress decoded;
    std::string reason;
};

struct TickResult
{
    std::uint64_t cycle = 0;
    std::vector<NetworkGrant> arrivedPostXbar;
    std::vector<NetworkGrant> acceptedByXbar;
    std::vector<NetworkStall> inputStalls;
    std::vector<BankGrant> bankGrants;
    std::vector<BankStall> bankStalls;
};

struct BankCandidate
{
    const Request *request = nullptr;
    DecodedAddress decoded;
    Command command = Command::Read;
};

class BankArbitrationPolicy
{
  public:
    virtual ~BankArbitrationPolicy() = default;

    virtual std::optional<std::size_t> choose(
        const std::vector<BankCandidate> &candidates,
        std::size_t physicalSubBank,
        std::uint64_t cycle) = 0;

    virtual std::string name() const = 0;
};

std::shared_ptr<BankArbitrationPolicy>
makeBankArbitrationPolicy(BankPolicyType type);

class XbarLogicModel
{
  public:
    explicit XbarLogicModel(
        XbarConfig config = {},
        std::shared_ptr<BankArbitrationPolicy> bankPolicy = nullptr);

    bool tryEnqueue(const Request &request);
    void enqueueOrThrow(const Request &request);

    TickResult tick();
    bool idle() const;

    const XbarConfig &params() const;
    std::uint64_t currentCycle() const;
    DecodedAddress decodeAddress(std::uint64_t address) const;

    std::size_t inputOccupancy(std::size_t inputPort, Command command) const;
    std::size_t pendingOccupancy(
        std::size_t physicalSubBank,
        Command command) const;

  private:
    struct PendingQueues
    {
        std::deque<Request> reads;
        std::deque<Request> writes;
    };

    struct NetworkCandidate
    {
        Request request;
        DecodedAddress decoded;
    };

    struct RrState
    {
        explicit RrState(std::size_t outputs = 0) : next(outputs, 0) {}

        std::vector<std::size_t> next;
    };

    void validateConfig() const;
    void validateRequest(const Request &request) const;

    std::deque<Request> &inputQueueFor(const Request &request);
    const std::deque<Request> &inputQueueFor(
        std::size_t inputPort,
        Command command) const;

    void deliverNetworkArrivals(TickResult &result);
    void arbitrateBanks(TickResult &result);
    void arbitrateNetworkPath(
        Command command,
        RrState &stage1Rr,
        RrState &stage2Rr,
        RrState &stage3Rr,
        TickResult &result);

    std::vector<NetworkCandidate> arbitrateStage(
        const std::vector<NetworkCandidate> &candidates,
        RrState &rrState,
        std::size_t outputCount,
        const std::string &stageName,
        std::size_t (*keyFn)(const NetworkCandidate &),
        TickResult &result) const;

    std::optional<std::size_t> chooseQoSThenRR(
        const std::vector<NetworkCandidate> &candidates,
        const std::vector<std::size_t> &candidateIndexes,
        RrState &rrState,
        std::size_t outputKey) const;

    static std::size_t stage1Key(const NetworkCandidate &candidate);
    static std::size_t stage2Key(const NetworkCandidate &candidate);
    static std::size_t stage3Key(const NetworkCandidate &candidate);

    bool postQueueCanAccept(const DecodedAddress &decoded, Command command) const;
    std::deque<Request> &pendingQueueFor(
        std::size_t physicalSubBank,
        Command command);
    const std::deque<Request> &pendingQueueFor(
        std::size_t physicalSubBank,
        Command command) const;

    XbarConfig config;
    std::uint64_t cycle = 0;

    std::vector<std::deque<Request>> readInputs;
    std::vector<std::deque<Request>> writeInputs;
    std::vector<PendingQueues> pendingByPhysicalSubBank;
    std::vector<NetworkGrant> oneCycleNetworkPipe;

    RrState readStage1Rr;
    RrState readStage2Rr;
    RrState readStage3Rr;
    RrState writeStage1Rr;
    RrState writeStage2Rr;
    RrState writeStage3Rr;

    std::shared_ptr<BankArbitrationPolicy> bankPolicy;
};

std::string commandName(Command command);
std::string bankPolicyName(BankPolicyType type);
std::string formatAddress(std::uint64_t value);

} // namespace gem5::xbarlogic
