/**
 * @file DynamicPortsModel.cpp
 * @brief Implementation of the DynamicPortsModel class for managing dynamic nodes and connections in the FSM editor.
 *
 * This file contains the implementation of the DynamicPortsModel class, which provides functionality
 * for creating, modifying, and serializing nodes and connections in the graphical FSM editor.
 * It supports adding/removing nodes and ports, managing node and connection data, and converting
 * the internal model to and from persistent representations (such as JSON or custom file formats).
 *
 * Key responsibilities:
 * - Managing the set of nodes and connections in the FSM editor.
 * - Providing methods to add, remove, and query nodes, ports, and connections.
 * - Handling serialization and deserialization of the FSM graph to/from JSON and custom formats.
 * - Supporting the conversion of the graphical model to an Automaton object for code generation.
 * - Emitting signals for UI updates when the model changes.
 *
 * Notable mentions:
 *  - DynamicPortsModel class has been inspired by the example provided in the
 *    github repository of the nodeeditor library "dynamic_ports"
 *    link: https://github.com/paceholder/nodeeditor/tree/master/examples
 * 
 * @author Jakub Kovařík, Josef Ambruz
 * @date 2025-5-11
 */

#include "DynamicPortsModel.hpp"
#include "spec_parser/automaton-parser.hpp"

DynamicPortsModel::DynamicPortsModel()
    : _nextNodeId{1}
{}

DynamicPortsModel::~DynamicPortsModel()
{
    //
}

std::unordered_set<NodeId> DynamicPortsModel::allNodeIds() const
{
    return _nodeIds;
}

std::unordered_set<ConnectionId> DynamicPortsModel::allConnectionIds(NodeId const nodeId) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(_connectivity.begin(),
                 _connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&nodeId](ConnectionId const &cid) {
                     return cid.inNodeId == nodeId || cid.outNodeId == nodeId;
                 });

    return result;
}

void DynamicPortsModel::forceNodeUiUpdate(NodeId const id)
{
   // Emit update and enforce consistent node dimensions
   Q_EMIT nodeUpdated(id);
   _nodeGeometryData[id].size.setWidth(290);
}

std::unordered_set<ConnectionId> DynamicPortsModel::connections(NodeId nodeId,
                                                                PortType portType,
                                                                PortIndex portIndex) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(_connectivity.begin(),
                 _connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&portType, &portIndex, &nodeId](ConnectionId const &cid) {
                     return (getNodeId(portType, cid) == nodeId
                             && getPortIndex(portType, cid) == portIndex);
                 });

    return result;
}

bool DynamicPortsModel::connectionExists(ConnectionId const connectionId) const
{
    return (_connectivity.find(connectionId) != _connectivity.end());
}

NodeId DynamicPortsModel::addNode(QString const nodeType)
{
    NodeId newId = newNodeId();

    // Create new node.
    _nodeIds.insert(newId);

    // Add a default name to the node
    QString nodeName = "State ";
    nodeName.append(QString::fromStdString(std::to_string(newId)));
    _nodeNames[newId] = nodeName;

    // Add a default code to the node:
    _nodeActionCodes[newId] = "# Enter code here:\n";

    // Initaialzie it as non-fian lstate
    _nodeFinalStates[newId] = false;

    Q_EMIT nodeCreated(newId);

    return newId;
}

bool DynamicPortsModel::connectionPossible(ConnectionId const connectionId) const
{
    return !connectionExists(connectionId);
}

void DynamicPortsModel::addConnection(ConnectionId const connectionId)
{
    _connectivity.insert(connectionId);

    // Add a default transition condition code (just a comment)
    _connectionCodes[connectionId] = "";
    // Add a default delay of 0ms
    _connectionDelays[connectionId] = 0;

    Q_EMIT connectionCreated(connectionId);
}

bool DynamicPortsModel::nodeExists(NodeId const nodeId) const
{
    return (_nodeIds.find(nodeId) != _nodeIds.end());
}

NodeId DynamicPortsModel::findNodeByName(QString const nodeName)
{
    for (const auto& pair : _nodeNames) {
        if (pair.second == nodeName) {
            return pair.first;
        }
    }
    return QtNodes::InvalidNodeId;
}

PortAddRemoveWidget *DynamicPortsModel::widget(NodeId nodeId) const
{
    auto it = _nodeWidgets.find(nodeId);
    if (it == _nodeWidgets.end()) {
        _nodeWidgets[nodeId] = new PortAddRemoveWidget(0,
                                                       0,
                                                       nodeId,
                                                       *const_cast<DynamicPortsModel *>(this));
    }

    return _nodeWidgets[nodeId];
}

QVariant DynamicPortsModel::nodeData(NodeId nodeId, NodeRole role) const
{
    Q_UNUSED(nodeId);

    QVariant result;

    switch (role) {
    case NodeRole::Type:
        result = QString("Default Node Type");
        break;

    case NodeRole::Position:
        result = _nodeGeometryData[nodeId].pos;
        break;

    case NodeRole::Size:
        result = _nodeGeometryData[nodeId].size;
        break;

    case NodeRole::CaptionVisible:
        result = true;
        break;

    case NodeRole::Caption:
        result = _nodeNames.at(nodeId);
        break;

    case NodeRole::Style: {
        auto style = StyleCollection::nodeStyle();
        result = style.toJson().toVariantMap();
    } break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
        result = _nodePortCounts[nodeId].in;
        break;

    case NodeRole::OutPortCount:
        result = _nodePortCounts[nodeId].out;
        break;

    case NodeRole::Widget: {
        result = QVariant::fromValue(widget(nodeId));
        break;
    }
    }

    return result;
}

bool DynamicPortsModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    bool result = false;

    switch (role) {
    case NodeRole::Type:
        break;
    case NodeRole::Position: {
        _nodeGeometryData[nodeId].pos = value.value<QPointF>();

        Q_EMIT nodePositionUpdated(nodeId);

        result = true;
    } break;

    case NodeRole::Size: {
        _nodeGeometryData[nodeId].size = value.value<QSize>();
        result = true;
    } break;

    case NodeRole::CaptionVisible:
        break;

    case NodeRole::Caption:
        _nodeNames[nodeId] = value.value<QString>();
        result = true;
        break;

    case NodeRole::Style:
        break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
        _nodePortCounts[nodeId].in = value.toUInt();
        widget(nodeId)->populateButtons(PortType::In, value.toUInt());
        break;

    case NodeRole::OutPortCount:
        _nodePortCounts[nodeId].out = value.toUInt();
        widget(nodeId)->populateButtons(PortType::Out, value.toUInt());
        break;

    case NodeRole::Widget:
        break;
    }

    return result;
}

QVariant DynamicPortsModel::portData(NodeId nodeId,
                                     PortType portType,
                                     PortIndex portIndex,
                                     PortRole role) const
{
    switch (role) {
    case PortRole::Data:
        return QVariant();
        break;

    case PortRole::DataType:
        return QVariant();
        break;

    case PortRole::ConnectionPolicyRole:
        return QVariant::fromValue(ConnectionPolicy::One);
        break;

    case PortRole::CaptionVisible:
        return true;
        break;

    case PortRole::Caption:
        if (portType == PortType::In)
            return QString::fromUtf8("Port In");
        else
            return QString::fromUtf8("Port Out");

        break;
    }

    return QVariant();
}

bool DynamicPortsModel::setPortData(
    NodeId nodeId, PortType portType, PortIndex portIndex, QVariant const &value, PortRole role)
{
    Q_UNUSED(nodeId);
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    Q_UNUSED(value);
    Q_UNUSED(role);

    return false;
}

bool DynamicPortsModel::deleteConnection(ConnectionId const connectionId)
{
    bool disconnected = false;

    // remove the connection code
    _connectionCodes.erase(connectionId);

    auto it = _connectivity.find(connectionId);

    if (it != _connectivity.end()) {
        disconnected = true;

        _connectivity.erase(it);
    };

    if (disconnected)
        Q_EMIT connectionDeleted(connectionId);

    return disconnected;
}

bool DynamicPortsModel::deleteNode(NodeId const nodeId)
{
    _nodeFinalStates.erase(nodeId);
    _nodeNames.erase(nodeId);
    _nodeActionCodes.erase(nodeId);

    // Delete connections to this node first.
    auto connectionIds = allConnectionIds(nodeId);
    for (auto &cId : connectionIds) {
        deleteConnection(cId);
    }

    _nodeIds.erase(nodeId);
    _nodeGeometryData.erase(nodeId);
    _nodePortCounts.erase(nodeId);
    _nodeWidgets.erase(nodeId);

    Q_EMIT nodeDeleted(nodeId);

    return true;
}

QJsonObject DynamicPortsModel::saveNode(NodeId const nodeId) const
{
    QJsonObject nodeJson;

    nodeJson["id"] = static_cast<qint64>(nodeId);

    {
        QPointF const pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();

        QJsonObject posJson;
        posJson["x"] = pos.x();
        posJson["y"] = pos.y();
        nodeJson["position"] = posJson;

        nodeJson["inPortCount"] = QString::number(_nodePortCounts[nodeId].in);
        nodeJson["outPortCount"] = QString::number(_nodePortCounts[nodeId].out);
    }

    return nodeJson;
}

QJsonObject DynamicPortsModel::save() const
{
    QJsonObject sceneJson;

    QJsonArray nodesJsonArray;
    for (auto const nodeId : allNodeIds()) {
        nodesJsonArray.append(saveNode(nodeId));
    }
    sceneJson["nodes"] = nodesJsonArray;

    QJsonArray connJsonArray;
    for (auto const &cid : _connectivity) {
        connJsonArray.append(QtNodes::toJson(cid));
    }
    sceneJson["connections"] = connJsonArray;

    return sceneJson;
}

void DynamicPortsModel::loadNode(QJsonObject const &nodeJson)
{
    NodeId restoredNodeId = static_cast<NodeId>(nodeJson["id"].toInt());

    _nextNodeId = std::max(_nextNodeId, restoredNodeId + 1);

    // Create new node.
    _nodeIds.insert(restoredNodeId);

    setNodeData(restoredNodeId, NodeRole::InPortCount, nodeJson["inPortCount"].toString().toUInt());
    setNodeData(restoredNodeId, NodeRole::OutPortCount,nodeJson["outPortCount"].toString().toUInt());

    {
        QJsonObject posJson = nodeJson["position"].toObject();
        QPointF const pos(posJson["x"].toDouble(), posJson["y"].toDouble());

        setNodeData(restoredNodeId, NodeRole::Position, pos);
    }

    Q_EMIT nodeCreated(restoredNodeId);
}

void DynamicPortsModel::load(QJsonObject const &jsonDocument)
{
    QJsonArray nodesJsonArray = jsonDocument["nodes"].toArray();



    for (QJsonValueRef nodeJson : nodesJsonArray) {
        loadNode(nodeJson.toObject());
    }

    QJsonArray connectionJsonArray = jsonDocument["connections"].toArray();

    for (QJsonValueRef connection : connectionJsonArray) {
        QJsonObject connJson = connection.toObject();

        ConnectionId connId = QtNodes::fromJson(connJson);

        // Restore the connection
        addConnection(connId);
    }
}

Automaton* DynamicPortsModel::ToAutomaton() const
{
    if(_startStateId == 0)
    {
        qWarning() << "Start state not set!";
        return nullptr;
    }

    Automaton* fsm = new Automaton();

    for(const NodeId& id :_nodeIds)
    {
        // find the data for the current NodeId
        auto nodeActionCode = _nodeActionCodes.find(id)->second;
        auto nodeName = _nodeNames.find(id)->second;
        auto isFinal = _nodeFinalStates.find(id)->second;

        fsm->setName(fsmName.toStdString());
        fsm->setDescription("Description"); // TODO
        fsm->addState(nodeName.toStdString(), nodeActionCode.toStdString());
        if(isFinal)
        {
            fsm->addFinalState(nodeName.toStdString());
        }
    }

    for(const ConnectionId& connId : _connectivity)
    {
        // extract the data for this transtiiton
        auto fromNodeName = _nodeNames.find(connId.outNodeId)->second;
        auto toNodeName = _nodeNames.find(connId.inNodeId)->second;
        auto transitionCode = _connectionCodes.find(connId)->second;
        auto transitionDelay = _connectionDelays.find(connId)->second;

        // create a transition isntance
        Transition trans;
        trans.fromState = fromNodeName.toStdString();
        trans.toState = toNodeName.toStdString();
        trans.condition = transitionCode.toStdString();
        trans.delay = transitionDelay;

        // add it to the fsm
        fsm->addTransition(trans);
    }

    for (const auto& varInfo : variables)
    {
        fsm->addVariable(varInfo.name, varInfo.value, varInfo.type);
    }

    // set the Start node
    auto startNodeName = _nodeNames.find(_startStateId)->second;
    fsm->setStartState(startNodeName.toStdString());

    return fsm;
}

void DynamicPortsModel::writeNodeData(ofstream& os, NodeId const nodeId) const
{
    const auto nodeName = nodeData(nodeId, NodeRole::Caption).value<QString>();
    const auto pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();
    const auto inPortCount  = nodeData(nodeId, NodeRole::InPortCount).value<unsigned int>();
    const auto outPortCount = nodeData(nodeId, NodeRole::OutPortCount).value<unsigned int>();

    os << "#" << nodeName.toStdString() << ";" << pos.x() << ";" << pos.y() << ";" << inPortCount << ";" << outPortCount << "\n";
}

void DynamicPortsModel::ToFile(std::string const filename) const
{
    auto automaton = this->ToAutomaton();

    ofstream out(filename);
    if (!out.is_open()) {
        cerr << "Failed to open file: " << filename << endl;
        return;
    }

    // writes node metadata to the beginning of the file in the following format:
    // #state_name;pos_x;pos_y;in_port_count;out_port_count
    for(const auto nodeId : _nodeIds)
    {
        writeNodeData(out, nodeId);
    }

    // AUTOMATON block
    out << "AUTOMATON " << automaton->getName() << "\n";
    out << "    DESCRIPTION \"" << automaton->getDescription() << "\"\n";
    out << "    START " << automaton->getStartName() << "\n";

    // Final states
    const auto& finals = automaton->getFinalStates();
    out << "    FINISH [";
    for (size_t i = 0; i < finals.size(); ++i) {
        out << finals[i];
        if (i != finals.size() - 1) out << ", ";
    }
    out << "]\n";

    // Variables block
    out << "    VARS\n";
    for (const auto& varInfo : automaton->getVariables())
    {
        auto type = Automaton::varDataTypeAsString(varInfo.type);
        out << "        " << type << " " << varInfo.name << " = " << varInfo.value << "\n";
    }
    out << "    END\n\n";

    // States
    for (const auto& [stateName, action] : automaton->getStates()) {
        out << "STATE " << stateName << "\n";
        out << "    ACTION\n";

        // Print action line by line (simulate user script with newlines)
        istringstream stream(action);
        string line;
        while (getline(stream, line)) {
            out << "        " << line << "\n";
        }

        out << "    END\n\n";
    }

    // Transitions
    for (const auto& t : automaton->getTransitions()) {
        out << "TRANSITION " << t.fromState << " -> " << t.toState << "\n";
        out << "    CONDITION " << t.condition << "\n";
        out << "    DELAY " << t.delay << "\n\n";
    }

    out << "END\n";
    out.close();
}

struct StateInfo {
    std::string name;
    int posX;
    int posY;
    int inPortCount;
    int outPortCount;
};

std::vector<StateInfo> loadStateInfo(const std::string& filename)
{
    std::string line;
    std::ifstream fstream(filename);
    std::vector<StateInfo> result;

    while(std::getline(fstream, line))
    {
        if(line.empty() || line[0] != '#')
            break;

        line = line.substr(1); // ignore the initial #
        std::stringstream ss(line);
        std::string token;
        StateInfo info;

        std::getline(ss, info.name, ';');

        std::getline(ss, token, ';');
        info.posX = std::stoi(token);

        std::getline(ss, token, ';');
        info.posY = std::stoi(token);

        std::getline(ss, token, ';');
        info.inPortCount = std::stoi(token);

        std::getline(ss, token, ';');
        info.outPortCount = std::stoi(token);

        result.push_back(info);
    }

    return result;
}

void trimLeadingWhitespace(std::string& str)
{
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

void DynamicPortsModel::FromFile(std::string const filename)
{
    Reset();

    // 1) Load node positions and port counts:
    auto statesInfo = loadStateInfo(filename);
    for(auto& s : statesInfo)
    {
        NodeId id = addNode();
        setNodeData(id, NodeRole::Position, QPointF(s.posX, s.posY));
        setNodeData(id, NodeRole::InPortCount, s.inPortCount);
        setNodeData(id, NodeRole::OutPortCount, s.outPortCount);
        SetNodeName(id, QString::fromStdString(s.name));

        forceNodeUiUpdate(id);
    }

    // 2) Load automaton internal data:
    Automaton automaton;
    AutomatonParser::FromFile(filename, automaton);
    for(auto& nodeId: _nodeIds)
    {
        std::string code = automaton.getStateAction((_nodeNames.at(nodeId).toStdString()));
        trimLeadingWhitespace(code);
        _nodeActionCodes[nodeId] = QString::fromStdString(code);
    }

    auto finalStateNames = automaton.getFinalStates();
    for(auto& nodeId: _nodeIds)
    {
        auto nodeName = _nodeNames.at(nodeId).toStdString();
        if(std::find(finalStateNames.begin(), finalStateNames.end(), nodeName) != finalStateNames.end())
        {
            _nodeFinalStates[nodeId] = true;
        }
    }

    auto startNodeName = automaton.getStartName();
    for(auto& nodeId: _nodeIds)
    {
        if(_nodeNames.at(nodeId).toStdString() == startNodeName)
        {
            _startStateId = nodeId;
            break;
        }
    }

    for(auto var : automaton.getVariables())
    {
        variables.push_back(var);
    }
    fsmName = QString::fromStdString(automaton.getName());

    // 3) Connect states with transitions
    QMap<NodeId, NodeId> nextFreeInPortIndex; // to keep track of each nodes used InPort indexes
    // iterate all nodes:
    for(auto& nodeId : _nodeIds)
    {
        // get vector transitions coming out from this node (out ports)
        auto transitionsFrom = automaton.getTransitionsFrom(_nodeNames.at(nodeId).toStdString());

        // iterate transition coming out of this node and make those connections:
        uint outPortIdx = 0;
        for(auto& t : transitionsFrom)
        {
            // find the NodeId by name of the node we are connecting to
            NodeId toStateNodeId = 0;
            for (const auto& [key, value] : _nodeNames)
                if (value ==  QString::fromStdString(t.toState) )
                    toStateNodeId = key;

            // find the first free InPort index of the node we want to make transition to:
            NodeId inPortIdx = nextFreeInPortIndex.value(toStateNodeId, 0); // default to 0

             // make the actual connection between two of the nodes:
            ConnectionId connId{ nodeId, outPortIdx, toStateNodeId, inPortIdx };
            addConnection(connId);

            // Update the next free in-port index for this node
            nextFreeInPortIndex[toStateNodeId] = inPortIdx + 1;

            // add action code and delay to connection
            _connectionCodes[connId] = QString::fromStdString(t.condition);
            _connectionDelays[connId] = t.delay;

            outPortIdx++;
        }
    }
}

void DynamicPortsModel::Reset()
{
    auto nodeIdsCopy = _nodeIds;
    for (auto nodeId : nodeIdsCopy)
    {
        deleteNode(nodeId);
    }

   

    _nodeActionCodes.clear();
    _nodeFinalStates.clear();
    _nodeGeometryData.clear();
    _nodeIds.clear();
    _nodeNames.clear();
    _nodeActionCodes.clear();
    _connectionCodes.clear();
    _connectionDelays.clear();
    _connectivity.clear();
    _nodePortCounts.clear();
    _nodeWidgets.clear();
    variables.clear();

    _startStateId = 0;
    _nextNodeId = 1;
}

void DynamicPortsModel::addPort(NodeId nodeId, PortType portType, PortIndex portIndex)
{
    // STAGE 1.
    // Compute new addresses for the existing connections that are shifted and
    // placed after the new ones
    PortIndex first = portIndex;
    PortIndex last = first;
    portsAboutToBeInserted(nodeId, portType, first, last);

    // STAGE 2. Change the number of connections in your model
    if (portType == PortType::In)
        _nodePortCounts[nodeId].in++;
    else
        _nodePortCounts[nodeId].out++;

    // STAGE 3. Re-create previouly existed and now shifted connections
    portsInserted();

    Q_EMIT nodeUpdated(nodeId);
}

void DynamicPortsModel::removePort(NodeId nodeId, PortType portType, PortIndex portIndex)
{
    // STAGE 1.
    // Compute new addresses for the existing connections that are shifted upwards
    // instead of the deleted ports.
    PortIndex first = portIndex;
    PortIndex last = first;
    portsAboutToBeDeleted(nodeId, portType, first, last);

    // STAGE 2. Change the number of connections in your model
    if (portType == PortType::In)
        _nodePortCounts[nodeId].in--;
    else
        _nodePortCounts[nodeId].out--;

    portsDeleted();

    Q_EMIT nodeUpdated(nodeId);
}
