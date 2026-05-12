# Mular

**Mular** is a next-generation native execution layer for macOS that enables running applications from other platforms (Android, Windows) **without traditional virtualization or emulation**.

It focuses on **direct execution, system bridging, and composited rendering**, providing a lightweight and high-performance alternative to conventional approaches.

---

## 🚀 Vision

Mular aims to become a unified runtime layer where:

- Android apps run as native macOS windows
- Windows applications integrate seamlessly into macOS
- Multiple platform runtimes coexist under a single system layer
- No full OS virtualization is required

---

## ✨ Key Features (Planned)

- ⚡ **Native execution approach** (no heavy VM)
- 🧩 **Multi-runtime support**
  - Android (ART / Java layer)
  - Windows (Win32 / compatibility layer)
- 🖼 **Custom compositor**
  - Unified rendering pipeline for all platforms
- 🔗 **System bridge layer**
  - Translates platform APIs → macOS (Darwin)
- 📦 **App lifecycle management**
- 🛠 **CLI tooling (`mup`)**

---

## 🛠 Dependencies

Muplar currently depends on the following tools and libraries:

| Dependency | Purpose |
|---|---|
| Android NDK | Provides Clang toolchain, linker, and Android system headers/libraries |
| aarch64-elf-binutils | ELF inspection and binary utilities (`readelf`, `objdump`, etc.) |
| CMake | Cross-platform build system |
| Ninja | Fast build backend used by CMake |
| Apple Hypervisor Framework | Native virtualization support on macOS |
| Xcode Command Line Tools | macOS compiler and development utilities |

---

### macOS Installation

#### Install Xcode Command Line Tools

```bash
xcode-select --install
```

### Install Homebrew Dependencies
```bash
brew install android-ndk cmake ninja aarch64-elf-binutils
```
Then export the environment variable:
``` bash
export ANDROID_NDK_HOME=/opt/homebrew/share/android-ndk
```
---

## 🏗 Architecture Overview
![Muplar Architecture](https://drive.google.com/uc?export=view&id=1NaxCQf9Gedzkexm8vMlpUI8O7F5fFFkG)

## 🏗 Data flow
![Muplar Flow](
https://drive.google.com/uc?export=view&id=1iHtdPoD2v1JAirY_NDO8dU2RXywjrJiO)