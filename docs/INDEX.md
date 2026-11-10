# Quill Documentation

Welcome to the Quill language documentation! This is your guide to learning and using Quill, a statically-typed language designed for fast, numeric computing.

## Quick Links

**New to Quill?** Start here:

- [Getting Started](01-GETTING-STARTED.md) — Installation and your first program
- [Language Guide](02-LANGUAGE-GUIDE.md) — Syntax and core features
- [Type System](03-TYPE-SYSTEM.md) — Understanding types
- [Standard Library](04-STDLIB.md) — Built-in functions
- [Examples](05-EXAMPLES.md) — Practical code samples

**Ready to dig deeper?**

- [Advanced Topics](06-ADVANCED.md) — C interoperability, performance optimization
- [API Reference](#api-reference) — Command-line usage

---

## What is Quill?

Quill is a **statically-typed, compiled language** built for performance. It features:

- ✨ **Clean, readable syntax** — Similar to modern languages but simpler
- 🚀 **Blazing fast** — Compiles to native machine code with near-C performance
- 🔢 **Math-focused** — First-class numeric types and math library
- 🔗 **C interoperable** — Call C functions directly or drop into raw C
- 💻 **Cross-platform** — Native support for Linux and Windows

### Language Features

- Statically typed (types checked at compile time)
- Variables: `let`, `mut`, `const`
- Functions with type signatures
- Control flow: `if/else`, `while` loops
- Fixed-size arrays and growable `Vec<T>` lists
- Strings and string operations
- Standard library with math functions
- C function interoperability with `extern` and `C_call`
- Module system with `import`

---

## API Reference

### Command-Line Usage

#### Compile to Native Binary (LLVM, Default)

```bash
quill-[os] myfile.qsc
```

Compiles supported Quill code through LLVM and Clang. Use `--backend c` as a compatibility fallback.

**Options:**
- `-o OUTPUT` — Specify output filename

**Examples:**
```bash
quill-linux --compile program.qsc          # Creates 'program' binary
quill-linux --compile program.qsc -o bin   # Creates 'bin' binary
```

#### Emit C Without Compiling

```bash
quill-[os] --emit-c myfile.qsc -o output.c
```

Generates C code without compiling. Useful for inspection or custom compilation.

#### Emit LLVM IR Without Compiling

```bash
quill-[os] --emit-llvm myfile.qsc -o output.ll
```

Use `--backend llvm` explicitly when selecting the backend in scripts that may also select C.

**Examples:**
```bash
quill-linux program.qsc -o program.c       # Creates 'program.c'
quill-linux program.qsc -o src/output.c   # Creates 'src/output.c'
```

#### Formatting Code

```bash
quill-[os] --format myfile.qsc
```

Formats Quill source code (applies to file in-place or specified output).

### Platform Binaries

- `quill-linux` — Linux 64-bit executable
- `quill-windows32.exe` — Windows 32-bit executable
- `quill-windows64.exe` — Windows 64-bit executable

---

## Common Tasks

### Create a New Program

```bash
echo 'say "Hello!";' > hello.qsc
quill-linux --compile hello.qsc
./hello
```

### Import a Module

```quill
import "mathlib.qsc";
import "../stdlib/math.qsc";
```

### Call C Functions

```quill
extern func sqrt(x: double): double;
say sqrt(16);  # → 4.0
```

### Define a Function

```quill
func add(a: number, b: number): number {
    return a + b;
}

say add(5, 3);  # → 8
```

### Use Arrays

```quill
let arr: number[10];
arr[0] = 42;
say arr[0];
```

### Loop with While

```quill
let i: number = 0;
while i < 10 {
    say i;
    i++;
}
```

---

## Supported Platforms

| Platform | Status | Binary |
|----------|--------|--------|
| Linux | ✅ Supported | `quill-linux` |
| Windows | ✅ Supported | `quill-windows32.exe`, `quill-windows64.exe` |
| macOS | ⏸️ Paused | — |

---

## Project Structure

```
Quill/
├── bin/
│   └── quill-linux          # Compiler binary
├── examples/
│   ├── palindrome.qsc       # Palindrome checker
│   ├── reverse.qsc          # String/array reversal
│   ├── rps.qsc              # Rock-Paper-Scissors
│   ├── twosum.qsc           # Two Sum problem
│   ├── exponent.qsc         # Exponent calculation
│   └── math.example.qsc     # Math library demo
├── stdlib/
│   ├── math/
│   │   └── math.qsc         # Math functions
│   └── random/
│       └── random.qsc       # Random number functions
├── docs/
│   ├── 01-GETTING-STARTED.md
│   ├── 02-LANGUAGE-GUIDE.md
│   ├── 03-TYPE-SYSTEM.md
│   ├── 04-STDLIB.md
│   ├── 05-EXAMPLES.md
│   ├── 06-ADVANCED.md
│   └── INDEX.md             # This file
├── extension/               # VS Code extension
├── src/                     # Compiler source (C++)
└── Readme.md
```

---

## Getting Help

1. **Check the examples** — See [examples/](../examples/)
2. **Read the guides** — Start with [Getting Started](01-GETTING-STARTED.md)
3. **Review the Language Guide** — [Language Guide](02-LANGUAGE-GUIDE.md)
4. **Explore the type system** — [Type System](03-TYPE-SYSTEM.md)
5. **Look at code examples** — [Code Examples](05-EXAMPLES.md)

---

## Contributing

To contribute to Quill:

1. Clone the repository: `git clone https://github.com/omo5454/Quill.git`
2. Build from source: `./build.sh`
3. Create a feature branch
4. Make your changes
5. Submit a pull request

---

## License

See the [repository](https://github.com/omo5454/Quill) for license information.

---

## Changelog

For version history and release notes, see the [Releases](https://github.com/omo5454/Quill/releases) page.

---

## Related Resources

- **Repository:** https://github.com/omo5454/Quill
- **Releases:** https://github.com/omo5454/Quill/releases
- **VS Code Extension:** Language support for `.qsc` files

---

## Learning Path

1. **Beginner:** Follow [Getting Started](01-GETTING-STARTED.md) then [Language Guide](02-LANGUAGE-GUIDE.md)
2. **Intermediate:** Learn [Type System](03-TYPE-SYSTEM.md) and review [Examples](05-EXAMPLES.md)
3. **Advanced:** Explore [Advanced Topics](06-ADVANCED.md) for C interop and optimization

---

Happy coding with Quill! 🚀
