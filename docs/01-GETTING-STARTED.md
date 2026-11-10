# Getting Started with Quill

Welcome to Quill! This guide will walk you through installation and running your first program.

## What is Quill?

Quill is a statically-typed programming language designed for **fast, numeric computing**. It compiles directly to native machine code via C, giving you performance close to C while maintaining a clean, readable syntax.

### Key Features

- **Built for Math & Numerics** — First-class `number`, `double`, and `char` types with a math-oriented standard library
- **Blazing Fast** — Compiles to native machine code with near-C performance
- **Statically Typed** — Type errors caught at compile time
- **Simple & Clean Syntax** — Readable code with optional semicolons and minimal boilerplate
- **Cross-Platform** — Native support for Linux and Windows
- **C Interop** — Call linked C functions with typed signatures or use a raw C statement when needed

## Installation

### Windows

1. Download `quill-windows32.exe` or `quill-windows64.exe` from the [Releases](https://github.com/omo5454/Quill/releases) page
2. Run the executable in PowerShell

### Linux

1. Download `quill-linux` from the [Releases](https://github.com/omo5454/Quill/releases) page
2. Make it executable and run:

```bash
chmod +x quill-linux
./quill-linux myfile.qsc
```

### macOS

macOS support is currently paused. The focus is on ensuring maximum stability on Linux and Windows.

## Your First Program

Create a file named `hello.qsc`:

```quill
say "Hello, Quill!";
```

Compile and run it:

```bash
./quill-linux hello.qsc
./hello
```

Or in one step with the `-o` flag to specify output:

```bash
./quill-linux -o hello_binary hello.qsc
./hello_binary
```

## Usage

### Compiling to a Native Binary

Compile to an executable with LLVM (this is the default):

```bash
quill-[os] myfile.qsc
```

The generated LLVM IR is temporary and removed after compilation. Use `--backend c` for programs that still require an unsupported LLVM feature.

### Transpiling to C Only

Generate C code without compiling:

```bash
quill-[os] --emit-c myfile.qsc -o output.c
```

This is useful if you want to inspect the generated C or compile with custom C flags.

### Emitting LLVM IR

Emit LLVM IR for inspection or assembly with `llvm-as`:

```bash
quill --emit-llvm myfile.qsc -o output.ll
llvm-as output.ll -o output.bc
```

The LLVM backend supports scalar values, vectors, fixed arrays, structs, calls, printing, conditionals, loops, and common string operations. Some runtime APIs and maps remain on the C backend for now.

### Output File Naming

By default, the executable name matches the input (e.g., `program.qsc` → `program`). Use `-o` to specify a custom name:

```bash
quill-[os] myfile.qsc -o my_output
```

## What's Next?

- [Learn the Language Syntax](02-LANGUAGE-GUIDE.md)
- [Understand the Type System](03-TYPE-SYSTEM.md)
- [Explore the Standard Library](04-STDLIB.md)
- [Run Example Programs](../examples/)
