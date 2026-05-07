# Muplar Runtime Roadmap

## Overview

Muplar is evolving from a simple ELF parser into a complete Android-compatible runtime environment.

This roadmap breaks the project into realistic phases, from ELF loading all the way to Android application execution.

---

# Phase 1 — ELF Inspection

## Goal

Read and understand ELF binaries.

## Completed

* ELF magic validation
* ELF64 validation
* AArch64 validation
* Program header parsing
* Segment flag parsing
* PT_LOAD detection
* Virtual memory mapping
* Entrypoint translation

## Optional Improvements

* Section header parser
* Better ELF pretty printer
* Dynamic section dump
* ELF notes parser
* Symbol table printer

## Result

Muplar can:

* Open ELF binaries
* Validate architecture
* Parse segments
* Load segments into virtual memory
* Translate ELF virtual addresses into host memory pointers

---

# Phase 2 — Real Virtual Memory

## Goal

Simulate actual Linux/Android process memory.

## To Do

### Memory Layout

* Page alignment
* Page-size abstraction
* Virtual address allocator
* Address collision detection
* Reserved memory regions

### Segment Mapping

* mmap()-style mapping
* Proper RX/RW permissions
* Zero-fill BSS regions
* Anonymous memory mapping
* Shared/private mappings

### Process Regions

* Stack memory region
* Heap memory region
* Thread-local storage (TLS)
* Guard pages

### Debugging

* Memory inspector
* Hex dump tools
* Virtual memory visualizer

## Result

ELF virtual memory behaves like a Linux process.

---

# Phase 3 — ELF Dynamic Linking

## Goal

Support dynamically linked ELF executables and shared libraries.

## To Do

### Dynamic ELF Parsing

* Parse `.dynamic`
* Parse `.dynsym`
* Parse `.dynstr`
* Parse `.hash`
* Parse `.gnu.hash`

### Relocations

* Parse `.rela.dyn`
* Parse `.rela.plt`
* Apply relocations
* Support RELA relocation types
* GOT relocation handling
* PLT relocation handling

### Symbol Resolution

* Symbol lookup
* Global symbol table
* Weak symbol support
* Lazy symbol binding
* Symbol versioning

### Shared Libraries

* Load `.so` files
* Dependency resolution
* Recursive library loading
* Android linker namespaces

## Result

Muplar can load dynamically linked Android/Linux binaries.

---

# Phase 4 — CPU Execution Engine

## Goal

Execute AArch64 machine instructions.

## Recommended First Approach

Build a simple ARM64 interpreter.

## To Do

### CPU State

* General-purpose registers
* Stack pointer (SP)
* Program counter (PC)
* Condition flags
* SIMD/Floating-point registers

### Instruction Pipeline

* Instruction fetch
* Instruction decode
* Instruction execution
* Branch handling
* Exception handling

### Memory Operations

* Load/store instructions
* Alignment checks
* Instruction memory access
* Data memory access

### Control Flow

* Function calls
* Returns
* Conditional branches
* Indirect branches

### Debugging

* CPU trace logs
* Register dump
* Instruction disassembler
* Breakpoints

## Result

Muplar can execute ARM64 instructions.

---

# Phase 5 — Linux/Android Syscalls

## Goal

Allow guest binaries to communicate with the host operating system.

## To Do

### Syscall Dispatcher

* ARM64 syscall decoding
* Syscall table
* Argument extraction
* Return value handling

### Core Syscalls

* exit
* read
* write
* open
* close
* mmap
* munmap
* brk
* futex
* clock_gettime
* nanosleep

### File System

* Virtual filesystem layer
* Path translation
* File descriptor management
* Sandboxing

### Threads

* clone
* pthread compatibility
* Futex support
* Scheduler abstraction

## Result

Guest binaries can perform real OS operations.

---

# Phase 6 — Android Runtime Support

## Goal

Support Android native libraries and bionic behaviors.

## To Do

### Bionic Compatibility

* libc compatibility
* libm compatibility
* pthread support
* Android TLS model
* errno handling

### Android Linker Features

* linker namespaces
* Android relocation quirks
* RELRO handling
* VDSO handling

### JNI Support

* JNI environment
* Java/native bridge
* Native method registration
* Object references

### Android Runtime Integration

* libandroid_runtime
* libnativehelper
* system properties
* logging support

## Result

Android native libraries begin working inside Muplar.

---

# Phase 7 — ART Integration

## Goal

Run Android Java/Kotlin applications.

## To Do

### ART Runtime

* ART loader
* dex parsing
* oat/vdex support
* Class loading
* Method execution

### Java Runtime

* Java object model
* Garbage collection
* Reflection
* Exceptions
* Threads

### Android Framework

* Binder IPC
* System services
* Package manager
* Activity manager
* Resource loading

### App Execution

* APK loading
* Application lifecycle
* Intent handling
* Multi-process apps

## Result

Android applications can execute inside Muplar.

---

# Phase 8 — Graphics and Hardware

## Goal

Support games and graphics-intensive Android applications.

## To Do

### Graphics

* EGL implementation
* OpenGL ES translation
* Vulkan translation
* GPU abstraction

### Windowing

* SurfaceFlinger equivalent
* Window manager
* Framebuffer management
* Rendering pipeline

### Audio/Input

* Audio output
* Touch input
* Keyboard input
* Sensor emulation

### Performance

* JIT compilation
* GPU acceleration
* Memory optimization
* Multithreading

## Result

Android games and graphical applications become possible.

---

# Recommended Near-Term Development Order

## Immediate Next Steps

1. ELF section header parsing
2. `.dynamic` parsing
3. Relocation parsing
4. BSS zero-fill support
5. ARM64 instruction decoder
6. `exit()` syscall support
7. Execute minimal ARM64 binaries

## Mid-Term Goals

1. Dynamic linker
2. Shared library support
3. Basic syscall layer
4. ARM64 interpreter
5. Android bionic compatibility

## Long-Term Goals

1. ART runtime support
2. APK execution
3. Graphics stack
4. Android framework services
5. Game compatibility

---

# Suggested Repository Structure

```text
muplar/
├── cli/
├── runtime/
│   ├── elf/
│   ├── memory/
│   ├── cpu/
│   ├── linker/
│   ├── syscall/
│   ├── android/
│   └── art/
├── platform/
│   └── android/
├── tests/
│   ├── elf/
│   ├── cpu/
│   ├── syscall/
│   └── assets/
├── tools/
└── docs/
```

---

# Final Vision

Muplar eventually becomes:

* An Android-compatible runtime
* An ELF loader
* An ARM64 execution environment
* A lightweight Android subsystem
* A possible alternative to WSL-style Android execution

---

# Current Status

Current milestone reached:

* ELF loading
* Segment mapping
* Virtual memory translation

Muplar is now officially beyond the "toy parser" stage and has entered real runtime-loader architecture territory.
