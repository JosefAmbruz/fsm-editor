/**
 * @brief Parse a saved file to an automaton instance
 * @author Jakub Kovarik
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include "automaton-data.hpp"
#include "automaton-parser.hpp"


enum class ParserState {
    EXPECT_AUTOMATON,
    EXPECT_DESCRIPTION,
    EXPECT_START,
    EXPECT_FINISH,
    EXPECT_VARS,
    INSIDE_VARS,
    EXPECT_STATE_OR_TRANSITION,
    EXPECT_STATE_ACTION,
    INSIDE_STATE_ACTION,
    EXPECT_TRANSITION_CONDITION,
    EXPECT_TRANSITION_DELAY,
    DONE
};

// Keyword check 
bool startsWith(const string& str, const string& prefix)
{
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
}

// Trim comments and whitespace
string trim(const string& str)
{
    size_t hash = str.find('#');
    string noComment = (hash != string::npos) ? str.substr(0, hash) : str;

    size_t first = noComment.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";

    size_t last = noComment.find_last_not_of(" \t\r\n");
    return noComment.substr(first, (last - first + 1));
}

// parse a text file to an automaton instance
void AutomatonParser::FromFile(std::string const filename, Automaton& automaton) 
{
    string rawLine;
    string line;
    ParserState state = ParserState::EXPECT_AUTOMATON;
    string currentState;
    Transition currentTransition;
    ifstream in(filename);

    while (std::getline(in, rawLine)) 
    {
        line = trim(rawLine);

        // ignore empty lines and lines starting with a # which are used for UI node info
        if (line.empty() || line[0] == '#')
            continue;

        switch (state) {
            case ParserState::EXPECT_AUTOMATON:
                if (startsWith(line, "AUTOMATON ")) {
                    automaton.setName(trim(line.substr(10)));
                    state = ParserState::EXPECT_DESCRIPTION;
                } else {
                    cerr << "Expected 'AUTOMATON', found: " << line << endl;
                }
                break;

            case ParserState::EXPECT_DESCRIPTION:
                if (startsWith(line, "DESCRIPTION ")) {
                    automaton.setDescription(trim(line.substr(12)));
                    state = ParserState::EXPECT_START;
                } else {
                    cerr << "Expected 'DESCRIPTION', found: " << line << endl;
                }
                break;

            case ParserState::EXPECT_START:
                if (startsWith(line, "START ")) {
                    automaton.setStartState(trim(line.substr(6)));
                    state = ParserState::EXPECT_FINISH;
                } else {
                    cerr << "Expected 'START', found: " << line << endl;
                }
                break;

            case ParserState::EXPECT_FINISH:
                if (startsWith(line, "FINISH ")) {
                    string rest = trim(line.substr(7));
                    rest.erase(remove(rest.begin(), rest.end(), '['), rest.end());
                    rest.erase(remove(rest.begin(), rest.end(), ']'), rest.end());

                    istringstream ss(rest);
                    string stateName;
                    while (getline(ss, stateName, ',')) {
                        automaton.addFinalState(trim(stateName));
                    }
                    state = ParserState::EXPECT_VARS;
                } else {
                    cerr << "Expected 'FINISH', found: " << line << endl;
                }
                break;

            case ParserState::EXPECT_VARS:
                if (line == "VARS") {
                    state = ParserState::INSIDE_VARS;
                } else {
                    cerr << "Expected 'VARS', found: " << line << endl;
                }
                break;

            case ParserState::INSIDE_VARS: {
                if (line == "END") {
                    state = ParserState::EXPECT_STATE_OR_TRANSITION;
                    break;
                }
                size_t eq = line.find('=');
                if (eq != string::npos) {
                    string typeAndName = trim(line.substr(0, eq));
                    size_t spacePos = typeAndName.find(' ');
                    string type = trim(typeAndName.substr(0, spacePos));
                    string name = trim(typeAndName.substr(spacePos + 1));
                    string value = trim(line.substr(eq + 1));
                    automaton.addVariable(name, value, Automaton::varDataTypeFromString(type));
                } else {
                    cerr << "Malformed VARS line: " << line << endl;
                }
                break;
            }

            case ParserState::EXPECT_STATE_OR_TRANSITION:
                if (startsWith(line, "STATE ")) 
                {
                    currentState = trim(line.substr(6));
                    automaton.addState(currentState);
                    state = ParserState::EXPECT_STATE_ACTION;
                } 
                else if (startsWith(line, "TRANSITION "))
                {
                    size_t arrow = line.find("->");
                    if (arrow != string::npos)
                    {
                        currentTransition.fromState = trim(line.substr(10, arrow - 10));
                        currentTransition.toState = trim(line.substr(arrow + 2));
                        state = ParserState::EXPECT_TRANSITION_CONDITION;
                    } 
                    else
                    {
                        cerr << "Malformed TRANSITION line: " << line << endl;
                    }
                } else if (line == "END") {
                    // Final END token
                    state = ParserState::DONE;
                } else {
                    cerr << "Expected 'STATE', 'TRANSITION', or 'END', found: " << line << endl;
                }
                break;
            case ParserState::EXPECT_STATE_ACTION:
                if (line == "ACTION") {
                    state = ParserState::INSIDE_STATE_ACTION;
                } else {
                    cerr << "Expected 'ACTION', found: " << line << endl;
                }
                break;

            case ParserState::INSIDE_STATE_ACTION:
                if (line == "END") {
                    state = ParserState::EXPECT_STATE_OR_TRANSITION;
                } else {
                    automaton.appendToAction(currentState, rawLine);
                }
                break;
            
            case ParserState::EXPECT_TRANSITION_CONDITION:
                if (startsWith(line, "CONDITION"))
                {
                    currentTransition.condition = trim(line.substr(9));
                    state = ParserState::EXPECT_TRANSITION_DELAY;
                } 
                else
                {
                    cerr << "Expected 'CONDITION', found: " << line << endl;
                }
                break;

            case ParserState::EXPECT_TRANSITION_DELAY:
                if (startsWith(line, "DELAY")) {
                    string delayStr = trim(line.substr(5));
                    size_t pos = 0;
                    try {
                        int delay = stoi(delayStr, &pos);
                        if (pos != delayStr.size()) {
                            cerr << "Invalid characters in DELAY" << delayStr << endl;
                        } else {
                            currentTransition.delay = delay;
                        }
                    } catch (const invalid_argument& e) {
                        cerr << "Invalid delay value: " << delayStr << endl;
                    } catch (const out_of_range& e) {
                        cerr << "DELAY value out of range: " << delayStr << endl;
                    }
                    automaton.addTransition(currentTransition);
                    state = ParserState::EXPECT_STATE_OR_TRANSITION;
                } else {
                    cerr << "Expected 'DELAY', found: " << line << endl;
                }
                break;

            case ParserState::DONE:
                break;
        }
    }
}
