#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <memory>
#include <array>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string read_file(json arguments);
void write_file(json arguments);
std::string run_cmd(json arguments);

int main(int argc, char* argv[]) {
    if (argc < 3 || std::string(argv[1]) != "-p") {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty()) {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    const char* api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char* base_url_env = std::getenv("OPENROUTER_BASE_URL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty()) {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }
    json messages = json::array();

    json msg;
    msg["role"] = "user";
    msg["content"] = prompt;
    
    messages.push_back(msg);
    while(true) {
        json request_body = {
            {"model", "openrouter/free"},
            {"messages", messages},
            {"tools", json::array({
                {
                    {"type", "function"},
                    {"function", {
                        {"name", "Read"},
                        {"description", "Read and return the contents of a file."},
                        {"parameters", {
                        {"type", "object"},
                            {"properties", {
                                {"file_path", {
                                    {"type", "string"},
                                    {"description", "The path to the file to read"}
                                }}
                            }},
                            {"required", json::array({"file_path"})}
                        }}
                    }}
                },
                {
                    {"type", "function"},
                    {"function", {
                        {"name", "Write"},
                        {"description", "Write content to a file"},
                        {"parameters", {
                            {"type", "object"},
                            {"required", json::array({"file_path", "content"})},
                            {"properties", {
                                {"file_path", {
                                    {"type", "string"},
                                    {"description", "the path of the file to write to"}
                                }},
                                {"content", {
                                    {"type", "string"},
                                    {"description", "the content to write to the file"}
                                }}
                            }}
                        }}
                    }}
                },
                {
                    {"type", "function"},
                    {"function", {
                        {"name", "Bash"},
                        {"description", "Execute a shell command"},
                        {"parameters", {
                            {"type", "object"},
                            {"required", json::array({"command"})},
                            {"properties", {
                                {"command", {
                                    {"type", "string"},
                                    {"description", "The command to execute."}
                                }}
                            }}
                        }}
                    }}
                }
            })}
        };

        cpr::Response response = cpr::Post(
            cpr::Url{base_url + "/chat/completions"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}
            },
            cpr::Body{request_body.dump()}
        );

        if (response.status_code != 200) {
            std::cerr << "HTTP error: " << response.status_code << std::endl;
            return 1;
        }

        json result = json::parse(response.text);

        if (!result.contains("choices") || result["choices"].empty()) {
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }

        json message = result["choices"][0]["message"];
        json msg_to_append;
        msg_to_append["role"] = "assistant";
        if(message.contains("tool_calls")) {
            msg_to_append["content"] = nullptr;
            msg_to_append["tool_calls"] = message["tool_calls"];
            messages.push_back(msg_to_append);

            
            json tool_calls = message["tool_calls"];
            for(json tool_call : tool_calls) {

                json result_msg;
                result_msg["role"] = "tool";

                std::string function_name = tool_call["function"]["name"].get<std::string>();

                std::string argument_str = tool_call["function"]["arguments"].get<std::string>();
                json arguments = json::parse(argument_str);

                if(function_name == "Read") {
                    //read file
                    std::string content = read_file(arguments);
                    result_msg["tool_call_id"] = tool_call["id"];
                    result_msg["content"] = content;
                }else if(function_name == "Write") {
                    //write file
                    write_file(arguments);
                    result_msg["tool_call_id"] = tool_call["id"];
                    result_msg["content"] = "File written successfully.";
                }else if(function_name == "Bash") {
                    std::string result = run_cmd(arguments);
                    result_msg["tool_call_id"] = tool_call["id"];
                    result_msg["content"] = result;
                }
                messages.push_back(result_msg);
            }
        }else {
            std::cout << message["content"].get<std::string>();
            break;
        }
    }

    return 0;
}

std::string read_file(json arguments) {
    std::string file_path = arguments["file_path"].get<std::string>();
    if(!std::filesystem::exists(file_path)) {
        return "Error: File not found at specified location";
    }

    std::ifstream file(file_path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void write_file(json arguments) {
    std::string file_path = arguments["file_path"].get<std::string>();
    std::string content = arguments["content"].get<std::string>();

    std::ofstream file(file_path);
    if(!file.is_open()) {
        throw std::runtime_error("Couldn't open file.");
    }

    file << content;
}

std::string run_cmd(json arguments) {
    std::string command = arguments["command"].get<std::string>();
    std::string result;
    char buffer[128];

    std::unique_ptr<FILE, decltype(&_pclose)> pipe (_popen(command.c_str(), "r"), _pclose);

    if(!pipe) return "Error: Failed to run the command";

    while(fgets(buffer, 128, pipe.get()) != nullptr) {
        result += buffer;
    }

    return result;
}