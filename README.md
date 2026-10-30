# memorymanager

A Windows-focused C++ project for inspecting and manipulating process memory, with a lightweight benchmarking harness for measuring read/write throughput.

## Overview

This repository contains a small memory management utility that:

- opens a target process handle
- reads and writes memory for local and remote processes
- prefers a fast local memcpy path when possible
- falls back to an NT-based path for more generic memory access
- benchmarks serial memory transfer performance
- optionally uses large pages when available

The code is centered around `memorymanager::process` and is implemented under the `Project1/memorymanager` directory.

## Project structure

- `Project1/main.cpp` — entry point
- `Project1/memorymanager/memory.hxx` — public process API and declarations
- `Project1/memorymanager/memory.cxx` — main implementation and benchmark logic
- `Project1/memorymanager/io.hxx` / `io.cxx` — file/IO helpers and Windows integration utilities
- `Project1/memorymanager/syscalls.hxx` / `syscalls.cxx` — syscall wrappers and process access helpers
- `Project1/memorymanager/transfer.cxx` — transfer buffer/pool logic
- `Project1.slnx` — solution configuration
- `out.txt` — sample output from runtime or benchmark execution

## Features

- Local memory access using direct copy operations
- Remote process memory access through NT-based routines
- Process validation and handle lifetime management
- Region cache reuse for efficient repeated access
- Benchmark utilities for read/write throughput calculations
- Large-page privilege handling for performance-oriented scenarios

## Requirements

- Windows 10 or newer
- Visual Studio 2022 with C++ desktop development tools
- x64 target configuration
- Windows SDK available through the Visual Studio toolchain

## Building

1. Open `Project1.slnx` in Visual Studio.
2. Restore/build the solution.
3. Build the project target for x64.
4. Run the resulting executable.

If you are using the project file directly, the Visual Studio project is located at:

- `Project1/Project1.vcxproj`

## Notes

This project is a focused C++ experiment and benchmarking utility rather than a general-purpose external library. It is designed around Windows internals and the underlying process memory APIs, so it is best suited for learning, experimentation, and low-level Windows memory access work.

## Example behavior

When executed, the program:

1. initializes its syscall layer
2. opens the current process
3. verifies read/write operations
4. runs serial benchmark checks
5. prints throughput figures for read and write speeds

This is intended to validate correct memory access and estimate transfer performance in a controlled local environment.

## License

This repository does not currently declare a license file. If you plan to distribute or reuse it publicly, add an appropriate open-source license before publishing.
