/**
 * @file interpret_generator.cpp
 * @brief Implementation of the InterpretGenerator class for generating Python FSM scripts from an Automaton.
 * @author Josef Ambruz
 * @date 2025-5-11
 */

#include "interpret_generator.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

// Helper to make a string safe as a Python identifier
QString sanitize_python_identifier(std::string name) {
    if (name.empty()) {
        return "_empty_name_placeholder_"; // Should not happen if names are required
    }

    // Replace spaces and invalid characters with underscores
    std::replace(name.begin(), name.end(), ' ', '_');
    std::replace(name.begin(), name.end(), '-', '_');
    std::replace(name.begin(), name.end(), '.', '_');

    // Replace comparison operators with shorthand equivalents
    static const std::vector<std::pair<std::string, std::string>> replacements = {
        {"<=", "le"}, {">=", "ge"}, {"<", "lt"}, {">", "gt"}, {"==", "eq"}, {"!=", "ne"}
    };

    for (const auto& [original, replacement] : replacements) {
        size_t pos = 0;
        while ((pos = name.find(original, pos)) != std::string::npos) {
            name.replace(pos, original.length(), replacement);
            pos += replacement.length();
        }
    }

    // Remove any characters not suitable for identifiers
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) {
                   return !isalnum(c) && c != '_';
               }), name.end());

    // Ensure it doesn't start with a digit (prepend underscore if so)
    if (isdigit(name[0])) {
        name = "_" + name;
    }

    // Basic check for Python keywords
    static const std::set<std::string> keywords = {"None", "True", "False", "def", "class", "if", "else", "elif", "for", "while", "return", "import", "from"};
    if (keywords.count(name)) {
        name += "_var";
    }

    return QString::fromStdString(name);
}

// Helper to create a Python string literal (e.g., "value" -> "\"value\"")
QString to_python_string_literal(const std::string& s) {
    std::string escaped_s = s;
    // Basic escaping: replace \ with \\ and " with \"
    size_t pos = 0;

    while ((pos = escaped_s.find('\\', pos)) != std::string::npos) {
        escaped_s.replace(pos, 1, "\\\\");
        pos += 2;
    }

    pos = 0;

    while ((pos = escaped_s.find('\"', pos)) != std::string::npos) {
        escaped_s.replace(pos, 1, "\\\"");
        pos += 2;
    }

    return "\"" + QString::fromStdString(escaped_s) + "\"";
}

// Helper to convert string value to Python literal (bool, int, float, or string)
QString to_python_value_literal(const std::string& val_str) {
    if (val_str.empty()) return "None"; // Maybe error instead?

    std::string lower_val = val_str;
    std::transform(lower_val.begin(), lower_val.end(), lower_val.begin(), ::tolower);

    if (lower_val == "true") return "True";
    if (lower_val == "false") return "False";

    // Try to parse as int
    try {
        size_t processed_chars;
        int i_val = std::stoi(val_str, &processed_chars);
        if (processed_chars == val_str.length()) { // Ensure entire string was parsed
            return QString::fromStdString(std::to_string(i_val));
        }
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {}

    // Try to parse as float
    try {
        size_t processed_chars;
        double d_val = std::stod(val_str, &processed_chars);
        if (processed_chars == val_str.length()) { // Ensure entire string was parsed
            std::ostringstream oss;
            oss << d_val; // Let C++ decide precision, or use std::fixed, std::setprecision
            return QString::fromStdString(oss.str());
        }
    } catch (const std::invalid_argument&) {
    } catch (const std::out_of_range&) {}

    // Default to string literal
    return to_python_string_literal(val_str);
}

QString transform_to_local_vars(const QString& code, const std::vector<VariableInfo>& variables) {
    QString result;

    for (const auto& var_info : variables) {
        QString var_name = QString::fromStdString(var_info.name);
        result += result +  var_name + " = variables.get('" + var_name + "')\n";
    }
    result += "\n";

    result += code;

    result += "\n";

    for (const auto& var_info : variables) {
        QString var_name = QString::fromStdString(var_info.name);
        result += "fsm.set_variable('" + var_name + "', " + var_name + ")\n";
    }

    //for (const auto& var_pair : variables) {
    //    QString var_name = QString::fromStdString(var_pair.first);
    //    result += "variables['" + var_name + "'] = " + var_name + "\n";
    //}
    return result;
}

// Helper to replace variable names in code with variables.get('<name>')
QString replace_variables_with_get(const QString& code, const std::vector<VariableInfo>& variables) {
    QString result = code;
    for (const auto& var_info : variables) {
        QString var_name = QString::fromStdString(var_info.name);
        // Using word boundaries to avoid partial replacements
        QRegularExpression re("\\b" + QRegularExpression::escape(var_name) + "\\b");
        result.replace(re, "variables.get('" + var_name + "')");
    }
    return result;
}


InterpretGenerator::InterpretGenerator(QObject *parent)
    : QObject{parent}
{

}

void InterpretGenerator::generate(const Automaton& automaton, const QString& output_filename) {
    QDir().mkpath(QFileInfo(output_filename).absolutePath()); // Ensure directory exists

    QFile file(output_filename);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to open file for writing:" << output_filename;
        return;
    }

    QTextStream outfile(&file);

    // --- Collect all function names ---
    std::map<QString, QString> functions;
    std::map<QString, QString> state_action;
    functions["condition_always_true"] = "return True"; // default condition with no action

    for (const auto& pair : automaton.getStates()) {
        if (!pair.second.empty()) { // action
            QString function_name = "action_" + sanitize_python_identifier(pair.first);
            QString action_code = transform_to_local_vars(QString::fromStdString(pair.second), automaton.getVariables());;
            QStringList lines = QString::fromStdString(pair.second).split('\n');

            if (!lines.isEmpty() && lines.first().startsWith("#name=")) {
                function_name = lines.first().mid(6).trimmed(); // Extract function name after %name=
            } else if (!lines.isEmpty() && lines.first().startsWith("# Enter code here:")) {
                action_code = "pass";
            } else if (action_code.isEmpty()) {
                action_code = "pass";
            }

            functions[function_name] = action_code;   // function to function body map
            state_action[QString::fromStdString(pair.first)] = function_name; // state to action name map
        } else {
            QString function_name = "action_" + sanitize_python_identifier(pair.first);
            
            functions[function_name] = "pass";   // function to function body map
            state_action[QString::fromStdString(pair.first)] = function_name; // state to action name map
        }
    }

    for (const auto& transition : automaton.getTransitions()) {
        if (!transition.condition.empty()) {
            QString function_name = "condition_" + sanitize_python_identifier(transition.condition);
            QString condition_code = replace_variables_with_get(QString::fromStdString(transition.condition), automaton.getVariables());
            condition_code = "return (" + condition_code + ")";

            functions[function_name] = condition_code;
        }
    }

    // --- Python code generation ---
    outfile << "from fsm_core import FSM, State, Transition\n";
    outfile << "import time\n";
    outfile << "import logging\n\n";

    outfile << "# --- FSM Name: " << QString::fromStdString(automaton.getName()) << " ---\n";
    if (!automaton.getDescription().empty()) {
        outfile << "# Description: " << QString::fromStdString(automaton.getDescription()) << "\n";
    }
    outfile << "\n";

    outfile << "# --- Define FSM Actions and Conditions ---\n\n";

    for (const auto& func : functions) {
        outfile << "def " << func.first << "(fsm, variables):\n";
        for (const auto& line : func.second.split('\n')) {
            outfile << "    " << line << "\n";
        }
        outfile << "\n\n";
    }

    outfile << "# --- Main FSM Execution ---\n";
    outfile << "if __name__ == \"__main__\":\n";
    outfile << "    # 1. Create the FSM instance\n";

    QString fsm_name = sanitize_python_identifier(automaton.getName());
    outfile << "    " << fsm_name << " = FSM()\n\n";

    outfile << "    # 2. Define States\n";
    std::unordered_map<string, string> states = automaton.getStates();

    for (const auto& state : states) {
        QString py_state_name = sanitize_python_identifier(state.first);
        outfile << "    state_" << py_state_name << " = State(\n";
        outfile << "        name=" << to_python_string_literal(state.first) << ",\n";


        outfile << "        action=" << state_action[QString::fromStdString(state.first)] << ",\n";
        outfile << "        is_start_state=" << (state.first == automaton.getStartName() ? "True" : "False") << ",\n";
        outfile << "        is_finish_state=" << (automaton.isFinalState(state.first) ? "True" : "False") << "\n";
        outfile << "    )\n";
    }

    outfile << "\n";

    outfile << "    # 3. Define Transitions\n";

    int tr_counter = 0;
    for (const auto& t : automaton.getTransitions()) {
        QString py_from_state = sanitize_python_identifier(t.fromState);
        QString py_to_state = sanitize_python_identifier(t.toState);
        QString tr_var_name = "tr_" + py_from_state + "_to_" + py_to_state + "_" + QString::number(tr_counter++);

        outfile << "    " << tr_var_name << " = Transition(\n";
        outfile << "        target_state_name=" << to_python_string_literal(t.toState) << ",\n";

        QString cond_func = "condition_always_true"; // Default
        if (!t.condition.empty()) {
            cond_func = "condition_" + sanitize_python_identifier(t.condition);
        }
        outfile << "        condition=" << cond_func << ",\n";

        outfile << "        delay=" << t.delay << ".0\n"; // Ensure it's a float
        outfile << "    )\n";
    }
    outfile << "\n";


    outfile << "    # 4. Add Transitions to States\n";

    tr_counter = 0; // Reset for matching variable names
    for (const auto& t : automaton.getTransitions()) {
        QString py_from_state_var = "state_" + sanitize_python_identifier(t.fromState);
        QString py_from_state_name_sanitized = sanitize_python_identifier(t.fromState);
        QString py_to_state_name_sanitized = sanitize_python_identifier(t.toState);
        QString tr_var_name = "tr_" + py_from_state_name_sanitized + "_to_" + py_to_state_name_sanitized + "_" + QString::number(tr_counter++);
        outfile << "    " << py_from_state_var << ".add_transition(" << tr_var_name << ")\n";
    }
    outfile << "\n";


    outfile << "    # 5. Add States to FSM\n";

    for (const auto& state : states) {
        QString state_name = sanitize_python_identifier(state.first);
        QString py_state_var = "state_" + state_name;
        outfile << "    " << fsm_name << ".add_state(" << py_state_var << ")\n";
    }
    outfile << "\n";


    outfile << "    # 6. Set Initial Variables\n";
    if (automaton.getVariables().empty()) {
        outfile << "    # No initial variables defined in specification.\n";
    }
    for (const auto& var_info : automaton.getVariables()) {
        outfile << "    " << fsm_name << ".set_variable("
                << to_python_string_literal(var_info.name) << ", "
                << to_python_value_literal(var_info.value) << ")\n";
    }
    outfile << "\n";

    outfile << "    # 7. Connect to client and Run the FSM\n";
    outfile << "    client_host = 'localhost'\n";
    outfile << "    client_port = 65432 # Default port, change if needed\n\n";
    outfile << "    print(f\"Starting FSM '" << fsm_name << "'...\")\n";
    outfile << "    " << fsm_name << ".connect_to_client(host=client_host, port=client_port)\n\n";
    outfile << "    if " << fsm_name << "._client_socket: # Check if connection was successful\n";
    outfile << "        try:\n";
    outfile << "            " << fsm_name << ".run()\n";
    outfile << "        except KeyboardInterrupt:\n";
    outfile << "            print(\"\\nFSM execution interrupted by user (Ctrl+C).\")\n";
    outfile << "            " << fsm_name << ".stop()\n";
    outfile << "        except Exception as e:\n";
    outfile << "            logging.error(f\"An unexpected error occurred during FSM execution: {e}\", exc_info=True)\n";
    outfile << "            " << fsm_name << ".stop()\n";
    outfile << "        finally:\n";
    outfile << "            " << fsm_name << ".stop() # Ensure stop is called\n";
    outfile << "            print(\"FSM runner script finished.\")\n";
    outfile << "    else:\n";
    outfile << "        print(\"FSM did not connect to a client. Exiting.\")\n";

    file.close();
}
