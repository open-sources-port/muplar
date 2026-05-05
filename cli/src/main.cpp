#include <iostream>
#include <elf_loader.h>

int main(int argc, char** argv) {
    std::cout << "Muplar CLI (mup)\n";

    if (argc < 2)
    {
        return 1;
    }

    muplar::runtime::elf::parse(argv[1]);

    return 0;
}
