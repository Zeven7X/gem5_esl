#include "WiepPlatformRegistry.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace
{

std::string yamlQuote(const std::string &value)
{
    std::ostringstream stream;
    stream << '"';
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it)
    {
        const unsigned char ch = static_cast<unsigned char>(*it);
        switch (ch)
        {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(ch) << std::dec;
            }
            else
            {
                stream << static_cast<char>(ch);
            }
        }
    }
    stream << '"';
    return stream.str();
}

const char *nodeKindName(WiepPlatformRegistry::NodeKind kind)
{
    return kind == WiepPlatformRegistry::NodeKind::Cluster ? "cluster" : "module";
}

const char *portDirectionName(WiepPlatformRegistry::PortDirection direction)
{
    return direction == WiepPlatformRegistry::PortDirection::Master ? "master" : "slave";
}

} // anonymous namespace

WiepPlatformRegistry &WiepPlatformRegistry::instance()
{
    static WiepPlatformRegistry registry;
    return registry;
}

WiepPlatformRegistry::WiepPlatformRegistry() {}

std::string WiepPlatformRegistry::registerNode(const WiepModBase *object,
                                               const std::string &hierarchicalName)
{
    if (object == NULL)
        throw std::invalid_argument("cannot register a null Wiep node");
    if (hierarchicalName.empty())
        throw std::invalid_argument("Wiep node ID must not be empty");

    std::lock_guard<std::mutex> lock(mutex_);

    std::map<const WiepModBase *, std::string>::const_iterator objectIt =
        nodeIdByObject_.find(object);
    if (objectIt != nodeIdByObject_.end())
        return objectIt->second;

    std::map<std::string, NodeRecord>::const_iterator idIt = nodesById_.find(hierarchicalName);
    if (idIt != nodesById_.end() && idIt->second.object != object)
    {
        throw std::runtime_error("duplicate Wiep node ID: " + hierarchicalName);
    }

    NodeRecord node;
    node.id = hierarchicalName;
    node.name = leafNameOf(hierarchicalName);
    node.parentId = parentIdOf(hierarchicalName);
    node.cppType = "";
    node.kind = NodeKind::Module;
    node.object = object;

    nodesById_[node.id] = node;
    nodeIdByObject_[object] = node.id;
    return node.id;
}

void WiepPlatformRegistry::markCluster(const WiepModBase *object)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<const WiepModBase *, std::string>::const_iterator objectIt =
        nodeIdByObject_.find(object);
    if (objectIt == nodeIdByObject_.end())
        throw std::runtime_error("cluster was not registered as a Wiep node");

    nodesById_[objectIt->second].kind = NodeKind::Cluster;
}

void WiepPlatformRegistry::setNodeCppType(const WiepModBase *object, const std::string &cppType)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<const WiepModBase *, std::string>::const_iterator objectIt =
        nodeIdByObject_.find(object);
    if (objectIt == nodeIdByObject_.end())
        throw std::runtime_error("node was not registered");

    nodesById_[objectIt->second].cppType = cppType;
}

std::string WiepPlatformRegistry::registerPort(const void *portObject, const WiepModBase *owner,
                                               const std::string &portName, PortDirection direction,
                                               std::int32_t index, const std::string &protocol)
{
    if (portObject == NULL)
        throw std::invalid_argument("cannot register a null Wiep port");
    if (owner == NULL)
        throw std::invalid_argument("Wiep port owner must not be null");
    if (portName.empty())
        throw std::invalid_argument("Wiep port name must not be empty");

    std::lock_guard<std::mutex> lock(mutex_);

    std::map<const void *, std::string>::const_iterator objectIt = portIdByObject_.find(portObject);
    if (objectIt != portIdByObject_.end())
        return objectIt->second;

    std::map<const WiepModBase *, std::string>::const_iterator ownerIt =
        nodeIdByObject_.find(owner);
    if (ownerIt == nodeIdByObject_.end())
        throw std::runtime_error("register the Wiep port owner before its ports");

    const std::string localName = localPortName(ownerIt->second, portName);
    const std::string id = makePortId(ownerIt->second, localName, index);
    std::map<std::string, PortRecord>::const_iterator idIt = portsById_.find(id);
    if (idIt != portsById_.end() && idIt->second.object != portObject)
        throw std::runtime_error("duplicate Wiep port ID: " + id);

    PortRecord port;
    port.id = id;
    port.ownerId = ownerIt->second;
    port.name = localName;
    port.protocol = protocol;
    port.direction = direction;
    port.index = index;
    port.object = portObject;

    portsById_[port.id] = port;
    portIdByObject_[portObject] = port.id;
    return port.id;
}

bool WiepPlatformRegistry::registerConnection(const void *masterPort, const void *slavePort,
                                              const std::string &protocol)
{
    if (masterPort == NULL || slavePort == NULL)
        throw std::invalid_argument("connection ports must not be null");

    std::lock_guard<std::mutex> lock(mutex_);
    std::map<const void *, std::string>::const_iterator masterIt = portIdByObject_.find(masterPort);
    std::map<const void *, std::string>::const_iterator slaveIt = portIdByObject_.find(slavePort);
    if (masterIt == portIdByObject_.end() || slaveIt == portIdByObject_.end())
    {
        throw std::runtime_error("both ports must be registered before bind registration");
    }

    const PortRecord &master = portsById_[masterIt->second];
    const PortRecord &slave = portsById_[slaveIt->second];
    if (master.direction != PortDirection::Master || slave.direction != PortDirection::Slave)
    {
        throw std::runtime_error("connection must be registered as master -> slave");
    }

    for (std::vector<ConnectionRecord>::const_iterator it = connections_.begin();
         it != connections_.end(); ++it)
    {
        if (it->sourcePortId == master.id && it->targetPortId == slave.id)
        {
            return false;
        }
    }

    ConnectionRecord connection;
    connection.id = master.id + "->" + slave.id;
    connection.sourcePortId = master.id;
    connection.targetPortId = slave.id;
    connection.protocol = protocol.empty() ? master.protocol : protocol;
    connections_.push_back(connection);
    return true;
}

std::string WiepPlatformRegistry::nodeId(const WiepModBase *object) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<const WiepModBase *, std::string>::const_iterator it = nodeIdByObject_.find(object);
    return it == nodeIdByObject_.end() ? "" : it->second;
}

std::string WiepPlatformRegistry::portId(const void *object) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<const void *, std::string>::const_iterator it = portIdByObject_.find(object);
    return it == portIdByObject_.end() ? "" : it->second;
}

bool WiepPlatformRegistry::dumpYaml(const std::string &path, const std::string &rootNodeId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (path.empty())
        return false;

    std::string root = rootNodeId;
    if (root.empty())
    {
        for (std::map<std::string, NodeRecord>::const_iterator it = nodesById_.begin();
             it != nodesById_.end(); ++it)
        {
            if (it->second.parentId.empty())
            {
                if (!root.empty())
                    return false;
                root = it->second.id;
            }
        }
    }
    if (root.empty() || nodesById_.find(root) == nodesById_.end())
        return false;

    std::ofstream output(path.c_str(), std::ios::out | std::ios::trunc);
    if (!output)
        return false;

    output << "schema_version: 1\n";
    output << "platform:\n";
    output << "  root_node: " << yamlQuote(root) << "\n";

    if (nodesById_.empty())
        output << "nodes: []\n";
    else
        output << "nodes:\n";
    for (std::map<std::string, NodeRecord>::const_iterator it = nodesById_.begin();
         it != nodesById_.end(); ++it)
    {
        const NodeRecord &node = it->second;
        output << "  - id: " << yamlQuote(node.id) << "\n";
        output << "    name: " << yamlQuote(node.name) << "\n";
        output << "    kind: " << nodeKindName(node.kind) << "\n";
        if (node.parentId.empty())
            output << "    parent: null\n";
        else
            output << "    parent: " << yamlQuote(node.parentId) << "\n";
        output << "    cpp_type: " << yamlQuote(node.cppType) << "\n";
        output << "    parameters: {}\n";
        output << "    visual: {}\n";
    }

    if (portsById_.empty())
        output << "ports: []\n";
    else
        output << "ports:\n";
    for (std::map<std::string, PortRecord>::const_iterator it = portsById_.begin();
         it != portsById_.end(); ++it)
    {
        const PortRecord &port = it->second;
        output << "  - id: " << yamlQuote(port.id) << "\n";
        output << "    owner: " << yamlQuote(port.ownerId) << "\n";
        output << "    name: " << yamlQuote(port.name) << "\n";
        if (port.index == NoPortIndex)
            output << "    index: null\n";
        else
            output << "    index: " << port.index << "\n";
        output << "    direction: " << portDirectionName(port.direction) << "\n";
        output << "    protocol: " << yamlQuote(port.protocol) << "\n";
    }

    if (connections_.empty())
        output << "connections: []\n";
    else
        output << "connections:\n";
    for (std::vector<ConnectionRecord>::const_iterator it = connections_.begin();
         it != connections_.end(); ++it)
    {
        output << "  - id: " << yamlQuote(it->id) << "\n";
        output << "    source: " << yamlQuote(it->sourcePortId) << "\n";
        output << "    target: " << yamlQuote(it->targetPortId) << "\n";
        output << "    protocol: " << yamlQuote(it->protocol) << "\n";
        output << "    parameters: {}\n";
        output << "    visual: {}\n";
    }

    return output.good();
}

std::vector<WiepPlatformRegistry::NodeRecord> WiepPlatformRegistry::nodes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeRecord> result;
    for (std::map<std::string, NodeRecord>::const_iterator it = nodesById_.begin();
         it != nodesById_.end(); ++it)
    {
        result.push_back(it->second);
    }
    return result;
}

std::vector<WiepPlatformRegistry::PortRecord> WiepPlatformRegistry::ports() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PortRecord> result;
    for (std::map<std::string, PortRecord>::const_iterator it = portsById_.begin();
         it != portsById_.end(); ++it)
    {
        result.push_back(it->second);
    }
    return result;
}

std::vector<WiepPlatformRegistry::ConnectionRecord> WiepPlatformRegistry::connections() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_;
}

void WiepPlatformRegistry::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    nodesById_.clear();
    nodeIdByObject_.clear();
    portsById_.clear();
    portIdByObject_.clear();
    connections_.clear();
}

std::string WiepPlatformRegistry::parentIdOf(const std::string &nodeId)
{
    const std::string::size_type separator = nodeId.rfind('.');
    return separator == std::string::npos ? "" : nodeId.substr(0, separator);
}

std::string WiepPlatformRegistry::leafNameOf(const std::string &id)
{
    const std::string::size_type separator = id.rfind('.');
    return separator == std::string::npos ? id : id.substr(separator + 1);
}

std::string WiepPlatformRegistry::localPortName(const std::string &ownerId,
                                                const std::string &portName)
{
    const std::string prefix = ownerId + ".";
    if (portName.compare(0, prefix.size(), prefix) == 0)
        return portName.substr(prefix.size());
    return portName;
}

std::string WiepPlatformRegistry::makePortId(const std::string &ownerId,
                                             const std::string &portName, std::int32_t index)
{
    std::ostringstream stream;
    stream << ownerId << "." << portName;
    if (index != NoPortIndex)
        stream << "[" << index << "]";
    return stream.str();
}
