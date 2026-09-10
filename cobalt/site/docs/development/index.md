Project: /youtube/cobalt/_project.yaml
Book: /youtube/cobalt/_book.yaml

# Development & Build Platforms Overview

Welcome to Cobalt Development! Cobalt supports multiple operating systems, hardware architectures, and build host environments.

This guide provides an overview of the supported platforms, their target hardware, intended use cases, and recommended development workflows.

---

## Supported Build Platforms & Setup Guides

| Platform / Tooling | Type | Target Architecture & Hardware | Primary Purpose & Intended Use | Setup Guide |
| :--- | :--- | :--- | :--- | :--- |
| **Linux** | Target OS | **x86_64 Desktop** (Ubuntu / X11 / Wayland) | **Developer Workstation Environment**. Provides the fastest compile-and-debug iteration cycle for core logic, Web APIs, and Starboard verification (`nplb`), without requiring embedded TV hardware. | [setup-linux.md](setup-linux.md) |
| **AOSP** | Target OS | **ARM / ARM64** (Android Reference Hardware, AOSP) | **AOSP Evergreen Reference Target**. Compiles the thin Cobalt Loader (`cobalt_loader`, `cobalt.apk`) to dynamically load Cobalt Core (`libcobalt.so` / `libcobalt.lz4`) from Evergreen packages. | [setup-aosp.md](setup-aosp.md) |
| **RDK** | Target OS | **ARM / ARM64** (Pay-TV Set-Top Boxes, STB reference hardware) | **Pay-TV & STB Platform Target**. Integrates Starboard for RDK (`evergreen-arm-hardfp-rdk`) for operator-managed set-top box deployments. | [setup-rdk.md](setup-rdk.md) |
| **Docker** | Host Tooling | **Linux x86_64 Host** (Containerized Builder) | **Build Environment Consistency**. Containerized build host tool that standardizes dependencies and tools across developer workstations to avoid host OS version conflicts. | [setup-docker.md](setup-docker.md) |

---

## Recommended Development Workflow

To maximize development efficiency, we recommend following a 3-stage development workflow:

1. **Stage 1: Core Logic & Web Feature Development**
   - **Platform**: `Linux` (`linux-x64x11`)
   - **Why**: Fastest build times, direct gdb/lldb integration, and immediate execution on your workstation. Run `nplb` to verify Starboard API compliance.

2. **Stage 2: Standardized Host Build Environment**
   - **Tooling**: `Docker`
   - **Why**: Use containerized build definitions to build for cross-compiled targets (like RDK or AOSP) without polluting or conflicting with your local host environment.

3. **Stage 3: Target Device & Media Integration Validation**
   - **Platform**: `AOSP` or `RDK`
   - **Why**: Final testing of hardware video/audio decoding, platform lifecycle (suspend/resume), remote control input handling, and system-level performance.

---

## Cobalt Evergreen Architecture: Partner Responsibilities & Prebuilt CRX Flow

Under Cobalt's Evergreen architecture, responsibilities are strictly divided between Google and SoC/OEM partners:

* **Google-Built (Cobalt Core & Updater)**: Google compiles, signs, and distributes all official Cobalt Core (`libcobalt.so` / `libcobalt.lz4`) packages in `.crx` format via [GitHub Releases](https://github.com/youtube/cobalt/releases). In production and certification, partners are required to use official Google Prebuilt CRX packages.
* **Partner-Built (Starboard & Loader)**: SoC and OEM partners implement the Starboard platform layer (`libstarboard.so`) and build the Cobalt Loader (`loader_app`, `crashpad_handler`).

---

## Evergreen Path Configuration & Vendor Deployment Setup

When deploying prebuilt Evergreen packages (unzipped CRX packages) onto target hardware, configure the following path parameters and environment variables.

### 1. Evergreen Path Parameter Mapping Reference

| Parameter / Variable | Category | Expected Target Path | Description |
| :--- | :--- | :--- | :--- |
| **`--evergreen_library`** | `elf_loader_sandbox` Switch | `<target_root>/app/cobalt/lib/libcobalt.so` (or `libcobalt.lz4`) | Points to the Cobalt Core shared library binary. |
| **`--evergreen_content`** | `elf_loader_sandbox` Switch | `<target_root>/app/cobalt/content` | Points to the web engine resource directory containing `cobalt_shell.pak`, `fonts/`, and `ssl/`. |
| **`--content`** | `loader_app` Switch | `<target_root>` | Overrides the base root directory for `kSbSystemPathContentDirectory`. `loader_app` will append `/app/cobalt/` to locate Slot 0. |
| **`kSbSystemPathContentDirectory`** | Starboard System Path API | `<target_root>` | Returned by `SbSystemGetPath()`. Used by `loader_app` to construct paths for `manifest.json`, `lib/`, and `content/`. |

#### Target Directory Structure (Cobalt 27.lts Slot 0 Layout):
```text
/usr/share/content/data/                   <-- Base Target Root (<target_root>)
├── loader_app                             <-- Partner-built Loader executable
├── elf_loader_sandbox                     <-- Sandbox test executable
└── app/
    └── cobalt/                            <-- Factory Slot 0 Directory
        ├── manifest.json                  <-- Evergreen manifest file
        ├── lib/
        │   └── libcobalt.so               <-- Core shared library
        └── content/
            ├── cobalt_shell.pak           <-- UI and web resources
            ├── fonts/                     <-- System fonts and fonts.xml
            └── ssl/                       <-- Root CA certificates
```

### 2. Deploying Prebuilt CRX Packages (Standard Partner Workflow)

For step-by-step instructions on deploying prebuilt `.crx` packages on Linux workstation environments, refer to **[setup-linux.md](setup-linux.md#running-in-evergreen-mode)**.

---

## Appendix: Compiling Custom Cobalt Core from Source (For Core Engine Debugging Only)

> [!CAUTION]
> **Mandatory CRX Requirement for SoC Partners:**
> SoC and OEM partners are strictly required to use official Google-built Prebuilt CRX packages for testing, QA, and certification.
> Compiling Cobalt Core (`libcobalt.so`) from source is reserved for internal engine development and core debugging.

When developing or debugging custom Cobalt Core engine modifications:

| Feature / Step | Evergreen Architecture (Linux / RDK) | Native APK Architecture (Android TV) |
| :--- | :--- | :--- |
| **Target Architecture** | **Evergreen Dynamic Slot** (`use_evergreen = true`) | **Standard Android Application** (`use_evergreen = false`) |
| **Primary Build Target** | **`cobalt`** and **`lz4_compress`** | **`cobalt_apk`** |
| **Compiled Artifacts** | `app/cobalt/lib/libcobalt.so` and `libcobalt.lz4` | `apks/Cobalt.apk` (`dev.cobalt.coat`, bundling `libchrobalt.so`) |
| **Debug & Verification** | **Direct Slot Replacement**. Unpack the CRX or access Slot 0 on device, and overwrite `libcobalt.so` (or `.lz4`) via SSH/SCP or `deploy_rdk.py --only-lib`. | **Full APK Reinstallation**. Do not modify filesystem shared libraries directly; reinstall the updated package via `adb install -r -d Cobalt.apk`. |
| **Host Compression** | Required when testing production compressed library loading (`lz4_compress`). | **Not used / Inapplicable**. Native packaging directly handles shared library compression in APK. |

### Live Replacement Safety Rules for Evergreen Platforms:
1. **Stop Active Processes First**: Kill running `loader_app` sessions before copying new files. Overwriting active mapped libraries without stopping the process causes immediate `Text file busy` failures or fatal kernel segmentation faults.
2. **Deploy Both Formats**: When testing unstripped builds, deploy both uncompressed (`.so`) and compressed (`.lz4`) library files simultaneously.
3. **Keep Resources Synchronized**: If modifications change interface definitions or resources, push updated `content/` resources (`cobalt_shell.pak`) alongside the library binary.

---

## Additional References

- [Supported Features](reference/supported-features.md)
- [Troubleshooting Guide](reference/troubleshooting.md)
