// cli/src/main.cpp
//
// Muplar CLI (mup)
//
// Before this change, main() called ExecutionContext::execute() which
// cast the AArch64 entrypoint to a C function pointer and jumped to it
// directly.  That crashed on the first Linux syscall.
//
// Now main() uses GuestRunner, which wraps the elfuse Hypervisor.framework
// pipeline: ELF load → guest memory init → stack build → vCPU create →
// syscall-translated run loop.

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "guest_runner.h"   // NEW: replaces ExecutionContext

// Keep ElfLoader available for inspection/debug builds
#include "elf_loader.h"

static void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " [--verbose] [--sysroot PATH]\n"
              << "              [--native-activity]\n"
              << "              [--host-window] [--host-window-ms VALUE]\n"
              << "              [--jni-call CLASS METHOD SIGNATURE]"
              << " [--jni-int VALUE ...]\n"
              << "              <elf-file> [args...]\n";
}

int main(int argc, char** argv)
{
    std::cout << "Muplar CLI (mup)\n";

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Parse muplar-level flags (before the ELF path)
    // -----------------------------------------------------------------------
    muplar::runtime::elf::GuestRunnerConfig cfg;
    int arg_start = 1;

    while (arg_start < argc && argv[arg_start][0] == '-') {
        std::string flag = argv[arg_start];

        if (flag == "--verbose" || flag == "-v") {
            cfg.verbose = true;
            ++arg_start;
        } else if ((flag == "--sysroot") && arg_start + 1 < argc) {
            cfg.sysroot = argv[arg_start + 1];
            arg_start  += 2;
        } else if (flag == "--native-activity") {
            cfg.native_activity = true;
            ++arg_start;
        } else if (flag == "--host-window") {
            cfg.host_window = true;
            ++arg_start;
        } else if ((flag == "--host-window-ms") && arg_start + 1 < argc) {
            cfg.host_window = true;
            try {
                cfg.host_window_linger_ms = std::stoi(argv[arg_start + 1], nullptr, 0);
            } catch (const std::exception&) {
                std::cerr << "Invalid --host-window-ms value: "
                          << argv[arg_start + 1] << "\n";
                return 1;
            }
            arg_start += 2;
        } else if ((flag == "--jni-call") && arg_start + 3 < argc) {
            cfg.jni_call.enabled = true;
            cfg.jni_call.class_name  = argv[arg_start + 1];
            cfg.jni_call.method_name = argv[arg_start + 2];
            cfg.jni_call.signature   = argv[arg_start + 3];
            arg_start += 4;
        } else if ((flag == "--jni-int" || flag == "--jni-arg") &&
                   arg_start + 1 < argc) {
            try {
                cfg.jni_call.int_args.push_back(
                    std::stoll(argv[arg_start + 1], nullptr, 0));
            } catch (const std::exception&) {
                std::cerr << "Invalid --jni-int value: "
                          << argv[arg_start + 1] << "\n";
                return 1;
            }
            arg_start += 2;
        } else if (flag == "--") {
            ++arg_start;
            break;
        } else {
            std::cerr << "Unknown flag: " << flag << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (arg_start >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    // -----------------------------------------------------------------------
    // Build the guest config
    // -----------------------------------------------------------------------
    cfg.elf_path = argv[arg_start];
    cfg.verbose = true;

    // argv[0] inside the guest = the binary path; followed by any user args.
    for (int i = arg_start; i < argc; ++i) {
        cfg.argv.push_back(argv[i]);
    }

    // -----------------------------------------------------------------------
    // Optional: use ElfLoader to print info before running (debug aid)
    // -----------------------------------------------------------------------
    try {
        muplar::runtime::elf::ElfLoader loader;
        auto image = loader.load(cfg.elf_path);

        std::cout << "ELF entry   : 0x" << std::hex << image.entry << "\n"
                  << "Load range  : 0x" << image.load_min
                  << " – 0x"            << image.load_max << "\n"
                  << "Segments    : " << std::dec << image.segments.size() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "ELF inspect error: " << e.what() << "\n";
        // Non-fatal — GuestRunner will re-validate internally via elfuse
    }

    // -----------------------------------------------------------------------
    // Run through elfuse Hypervisor.framework pipeline
    // -----------------------------------------------------------------------
    try {
        muplar::runtime::elf::GuestRunner runner;
        return runner.run(cfg);

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
