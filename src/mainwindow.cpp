/**
 * @file mainwindow.cpp
 * @brief Implementation of the MainWindow class, the main GUI for the FSM editor and runner.
 *
 * This file contains the implementation of the MainWindow class, which manages the application's
 * graphical user interface, node canvas, FSM client, and Python FSM process. It provides slots
 * for handling user interactions, updating the UI, managing FSM state, and communicating with
 * both the Python FSM interpreter and the FSM client.
 *
 * Key responsibilities:
 * - Initializing and managing the node editor and FSM model.
 * - Handling user actions such as adding states, editing transitions, and saving/loading scenes.
 * - Generating Python FSM scripts and launching the Python interpreter process.
 * - Managing TCP communication with the Python FSM server via FsmClient.
 * - Updating the UI in response to FSM and process events.
 *
 * @author Josef Ambruz, Jakub Kovařík
 * @date 2025-5-11
 */

#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "qcombobox.h"
#include "qmessagebox.h"

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionStyle;
using QtNodes::GraphicsView;
using QtNodes::NodeRole;
using QtNodes::StyleCollection;
using QtNodes::NodeId;

void MainWindow::initializeModel()
{
    NodeId id1 = graphModel->addNode();
    graphModel->setNodeData(id1, NodeRole::Position, QPointF(0, 0));
    graphModel->setNodeData(id1, NodeRole::InPortCount, 1);
    graphModel->setNodeData(id1, NodeRole::OutPortCount, 1);

    NodeId id2 = graphModel->addNode();
    graphModel->setNodeData(id2, NodeRole::Position, QPointF(300, 300));
    graphModel->setNodeData(id2, NodeRole::InPortCount, 1);
    graphModel->setNodeData(id2, NodeRole::OutPortCount, 1);

    graphModel->addConnection(ConnectionId{id1, 0, id2, 0});
}

QAction *createNodeAction(DynamicPortsModel &graphModel, GraphicsView &view)
{
    auto action = new QAction(QStringLiteral("Create Node"), &view);
    QObject::connect(action, &QAction::triggered, [&]() {
        // Mouse position in scene coordinates.
        QPointF posView = view.mapToScene(view.mapFromGlobal(QCursor::pos()));

        NodeId const newId = graphModel.addNode();
        graphModel.setNodeData(newId, NodeRole::Position, posView);
    });

    return action;
}

void MainWindow::initNodeCanvas()
{
    graphModel = new DynamicPortsModel();
    // Initialize and connect two nodes.
    initializeModel();

    // Create the QtNode scene
    nodeScene = new BasicGraphicsScene(*graphModel, this);

    qWarning() << "MODEF FROM SCENE " << &(nodeScene->graphModel());

    // Create a View for the scene
    auto* view = new GraphicsView(nodeScene, this);
    view->setContextMenuPolicy(Qt::ActionsContextMenu);
    view->insertAction(view->actions().front(), createNodeAction(*graphModel, *view));

    // Add the view with the QtNode scene to our UI
    auto* layout = new QVBoxLayout(ui->nodeCanvasContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view);
}

inline std::string trimToStdString(const QString& str) {
    return str.trimmed().toStdString();
}

std::vector<VariableInfo> MainWindow::getVariableRowsAsVector()
{
    // extract the variable names and values from the QMap property
    std::vector<VariableInfo> result;
    result.reserve(variables.size());

    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
    {
        auto varName = it.key().toStdString();
        auto varValue = it.value().varValue.toStdString();
        auto varType = Automaton::varDataTypeFromString(it.value().dropDown->currentText().toStdString());

        result.emplace_back(VariableInfo{ varName, varValue, varType });
    }

    return result;
}

/**
 *    MAIN WINDOW CTOR
 *  ========================================================================
 *  ========================================================================
 */


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    // client and fsm interpret initialization
    , fsmClient(new FsmClient(this))
    , pythonFsmProcess(new QProcess(this))
{
    // qt mandatory call
    ui->setupUi(this);

    // force the variable rows to start from top and not center
    // for some reason this cannot be set in UI editor...
    ui->hlayout_variables->layout()->setAlignment(Qt::AlignTop);

    // connect Add Variable button to to onAddWidget
    QObject::connect(
        ui->button_AddWidget, &QPushButton::clicked,
        this, &MainWindow::onAddWidget);

    // Initialize the canvas with the state machine
    initNodeCanvas();


    // Set up a shortcut for Ctrl+L to clean the log output text edit
    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+L"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        ui->textEdit_logOut->setText("");
    });

    connect(nodeScene, &BasicGraphicsScene::nodeClicked, this, &MainWindow::onNodeClicked);
    connect(nodeScene, &BasicGraphicsScene::selectionChanged, this, &MainWindow::onNodeSelectionChanged);
    connect(nodeScene, &BasicGraphicsScene::connectionClicked, this, & MainWindow::onConnectionClicked);
    connect(ui->actionSave_to_file, &QAction::triggered, this, &MainWindow::onSaveToFileClicked);
    connect(ui->actionOpen_from_file, &QAction::triggered, this, &MainWindow::onLoadFromFileClicked);

    // --- Connect FsmClient signals ---
    connect(fsmClient, &FsmClient::connected, this, &MainWindow::onFsmClientConnected);
    connect(fsmClient, &FsmClient::disconnected, this, &MainWindow::onFsmClientDisconnected);
    connect(fsmClient, &FsmClient::messageReceived, this, &MainWindow::onFsmClientMessageReceived);
    connect(fsmClient, &FsmClient::fsmError, this, &MainWindow::onFsmClientError);

    // --- Connect QProcess signals ---
    connect(pythonFsmProcess, &QProcess::finished, this, &MainWindow::onPythonProcessFinished);
    connect(pythonFsmProcess, &QProcess::errorOccurred, this, &MainWindow::onPythonProcessError);
    connect(pythonFsmProcess, &QProcess::stateChanged, this, &MainWindow::onPythonProcessStateChanged);
    connect(pythonFsmProcess, &QProcess::readyReadStandardOutput, this, &MainWindow::onPythonReadyReadStdOut);
    connect(pythonFsmProcess, &QProcess::readyReadStandardError, this, &MainWindow::onPythonReadyReadStdErr);
}

MainWindow::~MainWindow()
{
    // Ensure Python process is terminated if running
    if (pythonFsmProcess && pythonFsmProcess->state() != QProcess::NotRunning) {
        qInfo() << "MainWindow destructor: Terminating Python FSM process.";
        pythonFsmProcess->terminate();
        if (!pythonFsmProcess->waitForFinished(1000)) {
            pythonFsmProcess->kill();
            pythonFsmProcess->waitForFinished();
        }
    }
    // FsmClient and QProcess will be deleted due to parent-child relationship
    // if fsmClient is connected, its destructor should handle disconnection, hopefully.
    delete ui;
}


/**
 *    FSMCLIENT SLOTS
 *  ========================================================================
 *  ========================================================================
 */


void MainWindow::onFsmClientConnected() {
    qInfo() << "[MainWindow] Connected to FSM server!";
    // Update UI (e.g., enable "Send Variable" button, change status label)
    ui->textEdit_logOut->append("CLIENT: Connected to FSM server.");
}

void MainWindow::onFsmClientDisconnected() {
    qInfo() << "[MainWindow] Disconnected from FSM server.";
    // Update UI
    ui->textEdit_logOut->append("CLIENT: Disconnected from FSM server.");
}

void MainWindow::onFsmClientMessageReceived(const QJsonObject& msg) {
    qInfo() << "[MainWindow] Message from FSM:" << msg;

    QJsonDocument doc(msg);
    //ui->textEdit_logOut->append("FSM -> CLIENT: " + doc.toJson(QJsonDocument::Compact));

    // Connected to FSM
    if (msg.contains("type") && msg["type"].toString() == "FSM_CONNECTED") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("message")) {
                ui->textEdit_logOut->append("FSM: " + payload["message"].toString());
            }
        }
    }

    // Fsm Start
    if (msg.contains("type") && msg["type"].toString() == "FSM_STARTED") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("start_state")) {
                ui->textEdit_logOut->append("FSM: Started");
            }
        }
    }

    // State changed
    if (msg.contains("type") && msg["type"].toString() == "CURRENT_STATE") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("name")) {
                QString currentStateName = payload["name"].toString();
                ui->textEdit_logOut->append("FSM: Current State: " + currentStateName);                            // Logging
                ui->label_currentState->setText("Current State: " + currentStateName);  // Current State label

                NodeId currentFsmNodeId = graphModel->findNodeByName(currentStateName);
                if (currentFsmNodeId != QtNodes::InvalidNodeId) {

                }
            }
        }
    }

    // Transitioning
    if (msg.contains("type") && msg["type"].toString() == "TRANSITION_TAKEN") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("delay") && payload.contains("from_state") && payload.contains("to_state")) {
                QString currentStateName = payload["from_state"].toString();
                QString nextStateName = payload["to_state"].toString();
                QString delayMs = QString::number(payload["delay"].toInt());
                ui->textEdit_logOut->append("FSM: Transitioning: " + currentStateName + " -> " + nextStateName + ", delay: " + delayMs);
            }
        }
    }

    // Fsm stuck
    if (msg.contains("type") && msg["type"].toString() == "FSM_STUCK") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("state_name")) {
                QString currentStateName = payload["state_name"].toString();
                ui->textEdit_logOut->append("FSM: Stuck on " + currentStateName + " state. No valid transition possible.");
            }
        }
    }

    // Fsm finished
    if (msg.contains("type") && msg["type"].toString() == "FSM_FINISHED") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("finish_state")) {
                QString currentStateName = payload["finish_state"].toString();
                ui->textEdit_logOut->append("FSM: Finished, final state is " + currentStateName);
            }
        }
    }

    // Fsm error
    if (msg.contains("type") && msg["type"].toString() == "FSM_ERROR") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("message")) {
                QString error = payload["message"].toString();
                ui->textEdit_logOut->append("FSM: Error occured: " + error);
            }
        }
    }

    // Variable update
    if (msg.contains("type") && msg["type"].toString() == "VARIABLE_UPDATE") {
        if (msg.contains("payload") && msg["payload"].isObject()) {
            QJsonObject payload = msg["payload"].toObject();
            if (payload.contains("name") && payload.contains("value")) {
                QString name = payload["name"].toString();

                QJsonValue val = payload["value"];
                QString value;
                if (val.isString()) {
                    value = val.toString();
                } else if (val.isDouble()) {
                    value = QString::number(val.toDouble());
                } else if (val.isBool()) {
                    value = val.toBool() ? "true" : "false";
                } else if (val.isNull()) {
                    value = "null";
                } else {
                    value = QString::fromUtf8(QJsonDocument(QJsonObject{{"unknown_type", val}}).toJson(QJsonDocument::Compact));
                }

                ui->textEdit_logOut->append("FSM: Variable " + name + " changed to: " + val.toString());

                // handle the variable change
                onVariableUpdate(name, value);
            }
        }
    }

}

/**
 *    QPROCESS SLOTS
 *  ========================================================================
 *  ========================================================================
 */

void MainWindow::onPythonProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    qInfo() << "[MainWindow] Python FSM process finished. Exit code:" << exitCode << "Status:" << exitStatus;
    QString stdOut = pythonFsmProcess->readAllStandardOutput();
    QString stdErr = pythonFsmProcess->readAllStandardError();
    if(!stdOut.isEmpty()) ui->textEdit_logOut->append("PYTHON STDOUT (on finish):\n" + stdOut);
    if(!stdErr.isEmpty()) ui->textEdit_logOut->append("PYTHON STDERR (on finish):\n" + stdErr);

    // If client was connected, it will likely disconnect now or soon
    // Update UI to show FSM is not running
    // TODO: add correct label
    // ui->label_currentState->setText("Current FSM State: Not Running");
}

void MainWindow::onPythonProcessError(QProcess::ProcessError error) {
    qWarning() << "[MainWindow] Python FSM process error:" << error << pythonFsmProcess->errorString();
    ui->textEdit_logOut->append("PYTHON PROCESS ERROR: " + pythonFsmProcess->errorString());
    // Update UI
}

void MainWindow::onPythonProcessStateChanged(QProcess::ProcessState newState) {
    qInfo() << "[MainWindow] Python FSM process state changed to:" << newState;
    // You can update UI based on this, e.g., "Starting...", "Running..."
    switch (newState) {
        case QProcess::NotRunning:
            ui->textEdit_logOut->append("Python FSM process is not running.");
            break;
        case QProcess::Starting:
            ui->textEdit_logOut->append("Python FSM process is starting...");
            break;
        case QProcess::Running:
            ui->textEdit_logOut->append("Python FSM process is running.");
            break;
        default:
            ui->textEdit_logOut->append("Unknown Python FSM process state.");
            break;
    }
}

void MainWindow::onPythonReadyReadStdOut() {
    QByteArray data = pythonFsmProcess->readAllStandardOutput();
    qInfo() << "[PYTHON STDOUT] " << data.trimmed(); // Log to console/debug output
    ui->textEdit_logOut->append("PYTHON STDOUT: " + QString::fromUtf8(data)); // Also show in UI log
    // You could parse this for "Server ready" messages
}

void MainWindow::onPythonReadyReadStdErr() {
    QByteArray data = pythonFsmProcess->readAllStandardError();
    qWarning() << "[PYTHON STDERR] " << data.trimmed(); // Log to console/debug output
    ui->textEdit_logOut->append("PYTHON STDERR: " + QString::fromUtf8(data)); // Also show in UI log
}


void MainWindow::onFsmClientError(const QString& err) {
    qWarning() << "[MainWindow] FSM Client error:" << err;
    ui->textEdit_logOut->append("CLIENT ERROR: " + err);
    // Update UI
}




/**
 *    NODEEDITOR SIGNALS
 *  ========================================================================
 *  ========================================================================
 */

void MainWindow::onNodeClicked(NodeId const nodeId)
{
    qWarning() << "Node clicked! Id:" << nodeId;

    // Save the node id that was clicked
    lastSelectedNode = nodeId;

    // disable UI for connection settings
    ui->textEdit_connCond->setEnabled(false);
    ui->spinBox_transDelayMs->setEnabled(false);

    // Enable the code text edit and state name lineedit but disable conn edit
    ui->textEdit_actionCode->setEnabled(true);
    ui->lineEdit_stateName->setEnabled(true);
    // enable the button for setting start state
    ui->pushButton_setStartState->setEnabled(true);
    // enable checkbox fgor setting final state
    ui->checkBox_isFinal->setEnabled(true);


    // Set the textbox text to the code of clicked node
    auto nodeText = graphModel->GetNodeActionCode(nodeId);
    ui->textEdit_actionCode->setText(nodeText);
    auto nodeName = graphModel->GetNodeName(nodeId);
    ui->lineEdit_stateName->setText(nodeName);
    auto checkboxState = graphModel->GetNodeFinalState(nodeId) ? Qt::CheckState::Checked : Qt::CheckState::Unchecked;
    ui->checkBox_isFinal->setCheckState(checkboxState);

    // disable the Start State button if this state is already set as start
    if(graphModel->IsStartNode(nodeId))
    {
        ui->pushButton_setStartState->setEnabled(false);
        ui->pushButton_setStartState->setText("✅Start State");
    }
    else // otherwise set the normal text
    {
        ui->pushButton_setStartState->setEnabled(true);
        ui->pushButton_setStartState->setText("Set as Start State");
    }
}

void MainWindow::onNodeSelectionChanged()
{
    qWarning() << "Selection Changed!";

    // if there is nothing selected, disable the UI elems fro adjusting state settings
    if(nodeScene->selectedItems().empty())
    {
        ui->textEdit_actionCode->setEnabled(false);
        ui->lineEdit_stateName->setEnabled(false);
        ui->textEdit_connCond->setEnabled(false);
        ui->pushButton_setStartState->setEnabled(false);
        ui->checkBox_isFinal->setEnabled(false);
        ui->spinBox_transDelayMs->setEnabled(false);
    }
}

void MainWindow::onConnectionClicked(ConnectionId const connId)
{
    qWarning() << "Connection clicked!";
    // save the conn id
    lastSelectedConnId = connId;

    // enable the textEdit for the connection condition code:
    ui->textEdit_connCond->setEnabled(true);
    // enable the spinBox for transition delay
    ui->spinBox_transDelayMs->setEnabled(true);

    // while editing the connection id, disable state name line edit
    ui->lineEdit_stateName->setEnabled(false);
    // disable the textEdit for the code editing
    ui->textEdit_actionCode->setEnabled(false);
    // disable the finalState checkbox and start stateb btn:
    ui->checkBox_isFinal->setEnabled(false);
    ui->pushButton_setStartState->setEnabled(false);

    // add the current connection code:
    auto connCode = graphModel->GetConnectionCode(connId);
    ui->textEdit_connCond->setText(connCode);

    // update the spinbox for delay ms
    auto delayMs = graphModel->GetConnectionDelay(connId);
    ui->spinBox_transDelayMs->setValue(delayMs);
}

void MainWindow::onSaveToFileClicked()
{
    // parse the variable definition textbox contents
    graphModel->variables = getVariableRowsAsVector();

    QString filename = QFileDialog::getSaveFileName(nullptr,
                                                    "Open Fsm File",
                                                    QDir::homePath(),
                                                    "Fsm File (*.fsm)");

    if (filename.isEmpty())
        return;

    if (!filename.endsWith("fsm", Qt::CaseInsensitive))
        filename += ".fsm";

    graphModel->ToFile(filename.toStdString());
}

void MainWindow::updateUiFromGraphModel()
{
    // set FSM name lineEdit
    ui->lineEdit_fsmName->setText(graphModel->fsmName);

    // clear old variable rows in UI:
    for (auto it = variables.begin(); it != variables.end(); ++it)
    {
        VariableEntry& entry = it.value();
        QHBoxLayout* layout = entry.layout;

        // Remove and delete each widget from the layout
        while (layout->count() > 0)
        {
            QLayoutItem* item = layout->takeAt(0);
            if (QWidget* widget = item->widget())
                widget->deleteLater();  // safe deletion

            delete item;
        }
        // Delete the layout itself
        delete layout;
    }

    // Clear the map
    variables.clear();


    // iterate all loaded variables and add them to ui sidepanel:
    for(auto& varInfo : graphModel->variables)
    {
        auto name = QString::fromStdString(varInfo.name);
        auto value = QString::fromStdString(varInfo.value);
        addVariableRow(name, value, varInfo.type);
    }
}

void MainWindow::onLoadFromFileClicked()
{
    // Open .fsm file via file explorer:
    QString filename = QFileDialog::getOpenFileName(nullptr, "Open Fsm File", QDir::homePath(),"Fsm File (*.fsm)");

    if (filename.isEmpty())
        return;

    if (!filename.endsWith("fsm", Qt::CaseInsensitive))
        filename += ".fsm";

    // 1) load the node scene from the provided file:
    graphModel->FromFile(filename.toStdString());

    // 2) setup ui elements with the loaded automaton data
    updateUiFromGraphModel();
}


/**
 *    UI ELEMENTS SINGALS
 *  ========================================================================
 *  ========================================================================
 */

void MainWindow::on_button_Run_clicked()
{
    // --- 0. Stop any existing FSM and client ---
    if (pythonFsmProcess->state() != QProcess::NotRunning) {
        qInfo() << "[MainWindow] Stopping existing FSM process...";
        pythonFsmProcess->terminate(); // Or pythonFsmProcess->kill();
        if (!pythonFsmProcess->waitForFinished(3000)) { // Wait 3s
            qWarning() << "[MainWindow] Python FSM process did not terminate gracefully. Forcing kill.";
            pythonFsmProcess->kill();
            pythonFsmProcess->waitForFinished(); // Wait for kill
        }
        qInfo() << "[MainWindow] Previous FSM process stopped.";
    }

    // TODO: implement the isConnected method
    if (fsmClient->isConnected()) {
        qInfo() << "[MainWindow] Disconnecting existing client...";
        fsmClient->disconnectFromServer();
        // Wait for disconnection or just proceed, as new connection will override
    }

    // here update UI to maybe reflect "not running"

    // --- 1. Get Automaton Data ---

    // --- Variables ---

    graphModel->variables = getVariableRowsAsVector();

    std::unique_ptr<Automaton> automaton(graphModel->ToAutomaton());
    if (!automaton) {
        qWarning() << "[MainWindow] Failed to get automaton data from model.";
        // Show error to user
        return;
    }

    // --- 2. Generate Python FSM Code ---
    InterpretGenerator generator;
    QString baseDir = QCoreApplication::applicationDirPath();
    QString pythonFilePath = baseDir + "/interpret/output.py";
    QDir().mkpath(QFileInfo(pythonFilePath).path()); // Ensure directory exists

    qDebug() << "[MainWindow] Generating Python FSM at:" << pythonFilePath;
    generator.generate(*automaton, pythonFilePath);

    // --- 3. Run the generated Python file ---
    QString pythonExe = "python"; // this could be configurable
    QString logFilePath = baseDir + "/interpret/output.log";
    QDir().mkpath(QFileInfo(logFilePath).path()); // Ensure directory exists

    // Clear previous log content
    QFile logFile(logFilePath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logFile.close();
    }

    pythonFsmProcess->setStandardOutputFile(logFilePath, QIODevice::Append); // Append for multiple runs in one session
    pythonFsmProcess->setStandardErrorFile(logFilePath, QIODevice::Append);

    qInfo() << "[MainWindow] Starting Python FSM server process:" << pythonExe << pythonFilePath;

    pythonFsmProcess->start(pythonExe, QStringList() << pythonFilePath);
    if (!pythonFsmProcess->waitForStarted(5000)) { // 5 sec
        qWarning() << "[MainWindow] Failed to start Python process!";
        qWarning() << "[MainWindow] Python Process Error:" << pythonFsmProcess->errorString();
        qWarning() << "[MainWindow] Stderr:" << pythonFsmProcess->readAllStandardError();
        qWarning() << "[MainWindow] Stdout:" << pythonFsmProcess->readAllStandardOutput();
        // Show error to user
        return;
    }

    qInfo() << "[MainWindow] Python FSM process started successfully.";

    // --- 4. Connect C++ client to the interpret ---
    QTimer::singleShot(2000, this, [this]() { // QTimer means non-blocking delay
        qInfo() << "[MainWindow] Attempting to connect client to FSM server...";
        fsmClient->connectToServer("localhost", 65432);
    });

    // process.waitForFinished(-1); // Wait until finished
    // qDebug() << "Python script finished. Output written to:" << logFilePath;
}

void MainWindow::on_button_addState_clicked()
{
    auto id = graphModel->addNode();
    graphModel->setNodeData(id, NodeRole::Position, QPointF(0, 0));
    graphModel->setNodeData(id, NodeRole::OutPortCount, 1);
    graphModel->setNodeData(id, NodeRole::InPortCount, 1);

    graphModel->forceNodeUiUpdate(id);
}

void MainWindow::on_textEdit_actionCode_textChanged()
{
    // update the code of selected node:
    auto textEditContent = ui->textEdit_actionCode->toPlainText();
    graphModel->SetNodeActionCode(lastSelectedNode, textEditContent);
}

void MainWindow::on_lineEdit_stateName_textChanged(const QString &text)
{
    qWarning() << "Node name update!";
    // update the name of selected node:
    graphModel->SetNodeName(lastSelectedNode, text);
}


void MainWindow::on_textEdit_connCond_textChanged()
{
    auto connCode = ui->textEdit_connCond->toPlainText();
    graphModel->SetConnectionCode(lastSelectedConnId, connCode);
}


void MainWindow::on_checkBox_isFinal_stateChanged(int state)
{
    // state:
    // 0 = not-checked
    // 2 = checked
    auto isChecked = (state == 2);
    graphModel->SetNodeFinalState(lastSelectedNode, isChecked);
}


void MainWindow::on_pushButton_setStartState_clicked()
{
    graphModel->SetStartNode(lastSelectedNode);
    // disable the Start State button if this state is already set as start
    if(graphModel->IsStartNode(lastSelectedNode))
    {
        ui->pushButton_setStartState->setEnabled(false);
        ui->pushButton_setStartState->setText("✅Start State");
    }
}


void MainWindow::on_button_Stop_clicked()
{
    if (fsmClient && fsmClient->isConnected()) { // Check if client exists and is connected
        qInfo() << "[MainWindow] Sending STOP_FSM command to Python interpreter.";
        ui->textEdit_logOut->append("CLIENT -> FSM: Sending STOP_FSM command.");
        fsmClient->sendStopFsm();
        // The Python FSM should then shut down, which will likely cause
        // pythonFsmProcess to finish and the m_fsmClient to disconnect.
        // The onFsmClientDisconnected and onPythonProcessFinished slots will handle UI updates.
    } else {
        qWarning() << "[MainWindow] Cannot send STOP_FSM: Client not connected.";
        ui->textEdit_logOut->append("CLIENT: Cannot send STOP_FSM - not connected.");
    }
}


void MainWindow::on_lineEdit_fsmName_textChanged(const QString &text)
{
    graphModel->fsmName = text;
}


void MainWindow::on_spinBox_transDelayMs_valueChanged(int value)
{
    graphModel->SetConnectionDelay(lastSelectedConnId, value);
}

/**
 *    UI DYNAMIC VARIABLE WIDGET
 *  ========================================================================
 *  ========================================================================
 */


void MainWindow::addVariableRow(QString const name, QString const value, VarDataType type)
{
    // Create and setup elements that will be in each row
    auto* label = new QLabel(name, this);

    auto* lineEdit = new QLineEdit(this);
    lineEdit->setText(value);
    lineEdit->setFixedWidth(75);

    auto* dropDown = new QComboBox(this);
    dropDown->insertItem(0, "Int");
    dropDown->insertItem(1, "Double");
    dropDown->insertItem(2, "String");
    auto dropDownTypeStr = QString::fromStdString(Automaton::varDataTypeAsString(type));
    dropDown->setCurrentText(dropDownTypeStr);

    auto* updateBtn = new QPushButton("✅", this);
    updateBtn->setFixedWidth(25);

    auto* removeBtn = new QPushButton("❌", this);
    removeBtn->setFixedWidth(25);

    // Get the existing layout where all the variable rows are:
    QVBoxLayout* allRowsLayout = qobject_cast<QVBoxLayout*>(ui->hlayout_variables->layout());
    // cretae a new row layout for new variable
    QHBoxLayout* rowLayout = new QHBoxLayout(this);
    rowLayout->addWidget(label);
    rowLayout->addWidget(lineEdit);
    rowLayout->addWidget(dropDown);
    rowLayout->addWidget(updateBtn);
    rowLayout->addWidget(removeBtn);
    allRowsLayout->insertLayout(0, rowLayout);

    // Store everything in one struct
    variables[name] = VariableEntry{ rowLayout, lineEdit, dropDown, value };

    // Update button logic
    connect(updateBtn, &QPushButton::clicked, this, [this, name, lineEdit]() {
        onVariableValueChangedByUser(name, lineEdit->text());
    });

    // Remove button logic
    connect(removeBtn, &QPushButton::clicked, this, [this, name]() {
        onRemoveWidget(name);
    });
}

void MainWindow::onAddWidget()
{
    QString newVarName = ui->lineEdit_newVarName->text();
    if (newVarName.isEmpty())
        return;

    if (variables.contains(newVarName))
    {
        QMessageBox::warning(this, "Duplicate", "Variable with such name already exists!");
        return;
    }

    ui->lineEdit_newVarName->clear();

    // Add a new default variable : Int with value of 0.
    addVariableRow(newVarName, "0", VarDataType::Int);
}

void MainWindow::onVariableValueChangedByUser(const QString& variableName, const QString& newValue)
{
    // update the variable value n the QMap

    variables[variableName].varValue = newValue;

    // Determine the variable type from the dropdown
    VarDataType varType = Automaton::varDataTypeFromString(variables[variableName].dropDown->currentText().toStdString());
    QJsonValue jsonValue;
    switch (varType) {
        case VarDataType::Int:
            jsonValue = QJsonValue(newValue.toInt());
            break;
        case VarDataType::Double:
            jsonValue = QJsonValue(newValue.toDouble());
            break;
        case VarDataType::String:
        default:
            jsonValue = QJsonValue(newValue);
            break;
    }
    fsmClient->sendSetVariable(variableName, jsonValue);

    qWarning() << "User updated a variable " << variableName << ", new val: " << newValue;
}

void MainWindow::onVariableUpdate(const QString& variableName, const QString& newValue)
{
    variables[variableName].varValue = newValue;

    variables[variableName].lineEdit->setText(newValue);
}

void MainWindow::onRemoveWidget(const QString& varName)
{
    if (!variables.contains(varName))
        return;

    auto entry = variables.take(varName);

    // Remove all widgets in the row
    while (entry.layout->count()) {
        QLayoutItem* item = entry.layout->takeAt(0);
        delete item->widget();
        delete item;
    }
    delete entry.layout;
}
