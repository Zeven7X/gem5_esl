#ifndef GEM5_CLION_WORKSPACE_WIEPPLATFORMREGISTRY_H
#define GEM5_CLION_WORKSPACE_WIEPPLATFORMREGISTRY_H

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class WiepModBase;

class WiepPlatformRegistry
{
  public:
    enum class NodeKind
    {
        Module,
        Cluster
    };

    enum class PortDirection
    {
        Master,
        Slave
    };

    struct NodeRecord
    {
        std::string id;
        std::string name;
        std::string parentId;
        std::string cppType;
        NodeKind kind;
        const WiepModBase *object;
    };

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

    struct ConnectionRecord
    {
        std::string id;
        std::string sourcePortId;
        std::string targetPortId;
        std::string protocol;
    };

    static const std::int32_t NoPortIndex = -1;

    static WiepPlatformRegistry &instance();

    std::string registerNode(const WiepModBase *object, const std::string &hierarchicalName);
    void markCluster(const WiepModBase *object);
    void setNodeCppType(const WiepModBase *object, const std::string &cppType);

    std::string registerPort(const void *portObject, const WiepModBase *owner,
                             const std::string &portName, PortDirection direction,
                             std::int32_t index = NoPortIndex,
                             const std::string &protocol = "generic");

    bool registerConnection(const void *masterPort, const void *slavePort,
                            const std::string &protocol = "");

    std::string nodeId(const WiepModBase *object) const;
    std::string portId(const void *object) const;

    bool dumpYaml(const std::string &path, const std::string &rootNodeId = "") const;

    std::vector<NodeRecord> nodes() const;
    std::vector<PortRecord> ports() const;
    std::vector<ConnectionRecord> connections() const;

    // Intended for tests or constructing another platform in the same process.
    void clear();

  private:
    WiepPlatformRegistry();

    WiepPlatformRegistry(const WiepPlatformRegistry &) = delete;
    WiepPlatformRegistry &operator=(const WiepPlatformRegistry &) = delete;

    static std::string parentIdOf(const std::string &nodeId);
    static std::string leafNameOf(const std::string &id);
    static std::string localPortName(const std::string &ownerId, const std::string &portName);
    static std::string makePortId(const std::string &ownerId, const std::string &portName,
                                  std::int32_t index);

    mutable std::mutex mutex_;
    std::map<std::string, NodeRecord> nodesById_;
    std::map<const WiepModBase *, std::string> nodeIdByObject_;
    std::map<std::string, PortRecord> portsById_;
    std::map<const void *, std::string> portIdByObject_;
    std::vector<ConnectionRecord> connections_;
};

#endif // GEM5_CLION_WORKSPACE_WIEPPLATFORMREGISTRY_H
