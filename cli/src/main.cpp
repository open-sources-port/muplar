#include <iostream>
#include <string>
#include "process/ProcessManager.h"
#include "process/MuProcess.h"

bool endsWith(const std::string& str, const std::string& suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int main(int argc, char** argv) {
    std::cout << "Muplar CLI (mup)\n";

    if (argc < 3) {
        std::cout << "Usage: mup run <file>\n";
        return 1;
    }

    std::string command = argv[1];
    std::string target = argv[2];

    ProcessManager pm;
    std::shared_ptr<MuProcess> proc;

    if (command == "run") {
        if (endsWith(target, ".exe")) {
            proc = pm.createWindowsProcess(target);
        } else if (endsWith(target, ".apk")) {
            proc = pm.createAndroidProcess(target);
        } else {
            proc = pm.createHostProcess(target);
        }

        proc->start();
    } else {
        std::cout << "Unknown command: " << command << "\n";
    }

    return 0;
}
