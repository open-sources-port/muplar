#include <iostream>
#include "process/ProcessManager.h"
#include "process/MuProcess.h"
#include "Compositor.h"

int main(int argc, char** argv) {
    std::cout << "Muplar CLI (mup)\n";

    Compositor comp;

    if (!comp.init()) {
        return -1;
    }

    comp.run();
    comp.shutdown();

    return 0;
}
