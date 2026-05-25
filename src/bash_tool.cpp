// Bash tool implementation

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>

#include <sys/wait.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <json_utils.hpp>
#include <bash_tool.hpp>
#include <terminal.hpp>

namespace bash_tool {

namespace {

PermissionMode g_permission_mode = PermissionMode::Ask;

std::string capture_command_output(const std::string& command) {
    std::string result;
    FILE* fp = popen(command.c_str(), "r");
    if (!fp) {
        return std::string("Error: failed to execute command: ") + command;
    }

    char buf[4096];
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
        result += buf;
    }
    int status = pclose(fp);
    
    if (status != 0) {
        std::ostringstream oss;
        oss << "[Command exited with status " << WEXITSTATUS(status) << "]\n";
        result += oss.str();
    }
    return result;
}

} // namespace

PermissionMode get_permission_mode() {
    return g_permission_mode;
}

void set_permission_mode(PermissionMode mode) {
    g_permission_mode = mode;
}

std::string get_system_info() {
    std::ostringstream info;
    
    struct utsname uts;
    if (uname(&uts) == 0) {
        info << "OS: " << uts.sysname << " " << uts.release << " (" << uts.machine << ")\n";
    }
    
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        info << "Hostname: " << hostname << "\n";
    }
    
    long num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cpus > 0) {
        info << "CPU Cores: " << num_cpus << "\n";
    }
    
#ifdef __linux__
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        if (cpuinfo.is_open()) {
            std::string line;
            while (std::getline(cpuinfo, line)) {
                if (line.find("model name") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        std::string model = line.substr(pos + 1);
                        size_t start = model.find_first_not_of(" \t");
                        if (start != std::string::npos) model = model.substr(start);
                        info << "CPU: " << model << "\n";
                    }
                    break;
                }
            }
            cpuinfo.close();
        }
    }
    {
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open()) {
            std::string line;
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) {
                        info << "RAM: " << line.substr(pos + 1);
                    }
                    break;
                }
            }
            meminfo.close();
        }
    }
#elif defined(__APPLE__)
    {
        FILE* f = popen("sysctl -n machdep.cpu.brand_string 2>/dev/null", "r");
        if (f) {
            char b[256];
            if (fgets(b, sizeof(b), f)) {
                std::string m(b);
                size_t p = m.find('\n');
                if (p != std::string::npos) m.resize(p);
                if (!m.empty()) info << "CPU: " << m << "\n";
            }
            pclose(f);
        }
    }
    {
        FILE* f = popen("sysctl -n hw.memsize 2>/dev/null", "r");
        if (f) {
            char b[64];
            if (fgets(b, sizeof(b), f)) {
                try {
                    long long bytes = std::stoll(b);
                    info << "RAM: " << (bytes / (1024*1024)) << " MB\n";
                } catch (...) {}
            }
            pclose(f);
        }
    }
#endif
    
    {
        FILE* py = popen("python3 --version 2>&1 || python --version 2>&1", "r");
        if (py) {
            char b[128];
            if (fgets(b, sizeof(b), py)) info << "Python: " << b;
            pclose(py);
        }
    }
    
    return info.str();
}

std::string get_tool_definition() {
    std::string sys_info = get_system_info();
    std::string escaped_info = json_utils::json_escape(sys_info);
    
    std::string desc = "Execute a bash command in a background job on the local machine. The command runs on the current machine with the following environment: " + escaped_info;
    
    return std::string("[") +
        "\n  {" +
        "\n    \"type\": \"function\"," +
        "\n    \"function\": {" +
        "\n      \"name\": \"bash\"," +
        "\n      \"description\": \"" + desc + "\"," +
        "\n      \"parameters\": {" +
        "\n        \"type\": \"object\"," +
        "\n        \"properties\": {" +
        "\n          \"command\": {" +
        "\n            \"type\": \"string\"," +
        "\n            \"description\": \"The bash command to execute. Can be any valid bash command. Output is captured and returned.\"" +
        "\n          }" +
        "\n        }," +
        "\n        \"required\": [\"command\"]" +
        "\n      }" +
        "\n    }" +
        "\n  }" +
        "\n]";
}

std::string execute(const std::string& command) {
    return capture_command_output(command);
}

std::string format_response_json(const std::string& command,
                                  const std::string& output,
                                  bool success) {
    std::ostringstream json;
    json << "{\"role\":\"tool\",\"content\":[{\"type\":\"tool_response\","
         << "\"name\":\"bash\",\"command\":\"" << json_utils::json_escape(command) << "\","
         << "\"success\":" << (success ? "true" : "false") << ","
         << "\"response\":\"" << json_utils::json_escape(output) << "\"}]}";
    return json.str();
}

} // namespace bash_tool
