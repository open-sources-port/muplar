#include "execution_context.h"

#include <iostream>

namespace muplar::runtime::elf {

    using entry_fn_t = int (*)();

    int ExecutionContext::execute(
        const MappedElfImage& image
    ) {
        if (!image.entry_host) {
            std::cerr << "Invalid entrypoint\n";
            return -1;
        }

        auto entry =
            reinterpret_cast<entry_fn_t>(
                image.entry_host
            );

        std::cout << "Executing ELF...\n";

        int rc = entry();

        std::cout << "ELF returned: "
                << rc
                << "\n";

        return rc;
    }

}
