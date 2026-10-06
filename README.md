# FSM Editor & Interpreter

A graphical node-based editor and real-time execution environment for Finite State Machines (FSMs), built with modern **C++17**, **Qt (5/6)**, and **Python 3**.

---

## Overview

**FSM Editor** provides an end-to-end environment for visually authoring, configuring, simulating, and debugging finite state machines. The application bridges a high-performance C++ graphical node interface with a lightweight Python interpreter via an asynchronous TCP-based IPC protocol.

```
┌─────────────────────────────────┐           TCP Socket (JSON Protocol)          ┌───────────────────────────────────┐
│     C++ / Qt Desktop UI         │ <===========================================> │     Python Execution Core         │
│  - Interactive Node Canvas      │        State sync, telemetry & injection      │  - Asynchronous Subprocess        │
│  - Variable Inspector & Editor  │                                               │  - State Machine Dispatcher       │
│  - Python Code Generator        │ ──(generates output.py & launches)──────────> │  - Transition Evaluator           │
└─────────────────────────────────┘                                               └───────────────────────────────────┘
```

---

## Features

- **Visual Graph Editor**: Node-based canvas allowing intuitive creation of states, terminal/initial flags, and directional transition wiring with dynamic port management.
- **Automated Code Generation**: Compiles the visual state machine topology and logic on the fly into standalone Python scripts (`output.py`).
- **Real-Time Telemetry & IPC**: Built-in TCP client-server bridge streaming state changes, transition delays, and action executions live to the UI.
- **Live Variable Inspection & Mutation**: Monitor automaton variables in real time and dynamically inject modified values into running state machines.
- **Deadlock & Stall Detection**: Automatically identifies when an automaton enters a blocked or non-advancing state during execution.
- **Human-Readable File Serialization (`.fsm`)**: Save and load complete state machine designs using a custom human-readable specification format, storing graph layout alongside automaton logic.
- **External Client Interfacing**: Includes a standalone Python test client (`tests/fsm_client_test.py`) capable of connecting to the running state machine over TCP to send commands and log events.

---

## File Format (`.fsm`)

The editor saves and loads state machines using a clean, human-readable specification syntax:

```text
#State 2;339;225;1;1
#State 1;-17;70;1;1
AUTOMATON my_fsm
DESCRIPTION "Example State Machine"
START State_Initial
FINISH [State_Final]
VARS
Int counter = 0
String status = "idle"
STATE State_Initial
ACTION
    counter = counter + 1
    print(f"Counter: {counter}")
END
TRANSITION State_Initial -> State_Final
CONDITION counter >= 5
...
```

---

## Requirements

- **C++ Compiler**: C++17 compatible (`GCC 9+`, `Clang 10+`, or `MSVC 2019+`)
- **Build System**: CMake >= 3.16
- **GUI Framework**: Qt 5 or Qt 6 (`QtWidgets`, `QtNetwork`)
- **Runtime Interpreter**: Python 3.8+ (accessible via `python` in `PATH`)
- **Documentation (Optional)**: Doxygen

---

## Building the Project

The project follows standard modern CMake conventions with out-of-source builds:

### Using CMake

```bash
# 1. Clone the repository
git clone https://github.com/JosefAmbruz/fsm-editor.git
cd fsm-editor

# 2. Configure build
cmake -B build

# 3. Build target
cmake --build build -j$(nproc)

# 4. Run the editor
./build/fsm-editor
```

### Using Makefile (Convenience Wrapper)

A convenience `Makefile` is provided in the repository root:

```bash
make        # Runs cmake -B build && cmake --build build
make run    # Builds and launches the application
make clean  # Cleans build artifacts
```

---

## Usage

1. **Design the State Machine**:
   - Right-click or use the toolbar to add states.
   - Adjust input and output transition ports dynamically.
   - Connect output ports to target state inputs.
   - Define state actions and transition conditions in the editor panels.
2. **Manage Variables**:
   - Add global variables (Integers, Booleans, Strings, Floats) in the bottom-right panel.
3. **Execute & Simulate**:
   - Press the **🟢 Run** button to generate Python code, spawn the interpreter subprocess, and initiate real-time TCP telemetry.
   - Watch the active state display and live log stream.
   - Modify variable values on the fly to test branch conditions.
4. **Save and Load**:
   - Use the **File** menu to save the current model to `.fsm` format or reload existing designs.

---

## Communication Protocol

The GUI communicates with the Python interpreter over TCP using a structured newline-delimited JSON protocol.

- **FSM → Client Events**: `FSM_CONNECTED`, `FSM_STARTED`, `CURRENT_STATE`, `TRANSITION_TAKEN`, `VARIABLE_UPDATE`, `FSM_FINISHED`, `FSM_ERROR`
- **Client → FSM Commands**: `SET_VARIABLE`, `STOP_FSM`

For the full payload specification, see [CommunicationProtocol.md](CommunicationProtocol.md).

---

## Project Structure

```
├── CMakeLists.txt              # Root CMake project configuration
├── Makefile                    # Developer build convenience wrapper
├── CommunicationProtocol.md    # TCP IPC message specification
├── Doxyfile                    # Doxygen documentation configuration
├── src/
│   ├── CMakeLists.txt          # Target definition and install rules
│   ├── main.cpp                # Application entry point
│   ├── mainwindow.{cpp,h,ui}   # Main Qt GUI window and node scene logic
│   ├── DynamicPortsModel.{cpp,hpp} # Custom dynamic port graph model
│   ├── PortAddRemoveWidget.{cpp,hpp} # Dynamic port UI controller
│   ├── client.{cpp,hpp}        # Qt TCP socket client for FSM communication
│   ├── interpret_generator.{cpp,h} # Python code generation engine
│   ├── spec_parser/            # Custom .fsm parser and data structures
│   ├── interpret/              # Python runtime interpreter (fsm_core)
│   └── nodeeditor-master/      # Customized Qt node-editor framework
├── tests/
│   └── fsm_client_test.py      # Standalone Python CLI client for IPC testing
└── assets/                     # Application visual resources
```

---

## Documentation

To generate HTML API documentation using Doxygen:

```bash
doxygen Doxyfile
# Or via Makefile:
make doxygen

# Open generated docs:
xdg-open doc/html/index.html
```

---

## Code Attribution & References

- The visual graph framework is built upon the [`nodeeditor`](https://github.com/paceholder/nodeeditor) library, with customized modifications to support dynamic ports and custom serialization in this project.
- Dynamic port handling was inspired by the `dynamic_ports` architecture provided in the `nodeeditor` examples.

---

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

---

## Authors

- **Josef Ambruz**
- **Jakub Kovařík**
