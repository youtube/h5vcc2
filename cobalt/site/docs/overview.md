Project: /youtube/cobalt/_project.yaml
Book: /youtube/cobalt/_book.yaml

# Getting started

Cobalt is a lightweight, fully-capable HTML5/CSS/JS web platform powered by
Chromium and Blink. Designed for resource-constrained and embedded devices, it
delivers a rich, low-latency user experience and a modern application
development environment. By supporting modern web standards with a minimal
resource footprint (deployment size, RAM, CPU, GPU), Cobalt enables consistent,
high-performance applications across a wide variety of platforms.

#### Target audiences

Cobalt's documentation is written with two audiences in mind:

*   **Porters** enable Cobalt to run on new platforms by using Starboard,
    Cobalt's porting layer and OS abstraction. Each Starboard module
    (such as memory, socket, and thread) defines functions that porters must
    implement.

*   **Developers** want to build applications in familiar environments with
    advanced debugging tools without having to worry about compatibility with
    a highly fragmented set of browsers. At the same time, they want to have
    full control over their codebase so that they can ship features for
    constrained platforms, like TVs.

## Benefits of Cobalt

Cobalt significantly reduces the cost of supporting a browser on non-standard,
resource-constrained platforms. Because Cobalt operates at a consolidated,
versioned platform abstraction layer, porting takes only a few weeks, and subsequent
rebases are straightforward.

These are some other benefits that Cobalt provides:

*   **Broad portability**

    *   Does not require platforms to support JIT compilation and can run on
        platforms that disallow execution of dynamically generated code.
    *   Operates as a single-process application and does not rely on the
        ability to spawn multiple processes.
    *   Optimizes graphics performance for platforms with limited or no runtime
        shader compilation support.

*   **Automatic updates (Evergreen)**

    *   Features an Evergreen architecture enabling self-updating capabilities
        in the field, decoupling the web engine lifecycle from system OTAs to
        ensure rapid rollout of new features, optimizations, and critical
        security patches while minimizing ecosystem fragmentation.

*   **Small footprint**

    *   Uses a streamlined Chromium/Blink runtime, stripped of standard
        desktop browser overhead (like multi-process architecture and generic
        browser UI) to ensure a minimal footprint while delivering full modern
        web standards compatibility.
    *   Employs strict memory budgeting and resource management to ensure
        stability and prevent out-of-memory crashes on low-end devices.

*   **Optimized Performance**

    *   Leverages threaded compositing to ensure smooth, consistent 60FPS
        animations (such as transforms and opacity) by running them on a
        separate thread to avoid blocking the main thread.
    *   Takes advantage of multi-core CPUs by distributing tasks like
        rendering, resource loading, and layout across multiple threads,
        minimizing thread contention and ensuring responsive user input.
    *   Utilizes hardware-accelerated rendering (via Skia) to offload painting
        operations to the GPU, avoiding expensive CPU painting where possible.

*   **Standard developer tools**

    *   Supports fully-featured Chrome DevTools out-of-the-box, enabling
        developers to use standard web development tools for debugging
        JavaScript, inspecting the DOM, profiling memory, and analyzing
        performance.

## Getting started

### Porters

Porters should begin with the [porting guide](starboard/porting.md), which
explains how to use Starboard, Cobalt's porting layer, to customize the
platform-specific functionality that Cobalt uses. There are several reference
documents to help porters customize configuration files and to implement
module-specific functionality. The [Testing with NPLB](starboard/testing.md)
document provides an overview of Starboard's compliance test suite.

> Note: Cobalt requires a compliant C++17 compiler.

### Developers

Developers can use the setup instructions for
[Linux](development/setup-linux.md),
[RDK](development/setup-rdk.md), or
[AOSP](development/setup-aosp.md) to set up their Cobalt development
environment, clone the repository, and build a binary.
