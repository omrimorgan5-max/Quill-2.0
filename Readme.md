# Quill
---

## Why Quill?

- **Built for Math & Numerics** — First-class `number`, `double`, and `char` types, clean arithmetic, and a growing math-oriented standard library. Ideal for algorithms, calculators, simulations, and performance-sensitive numeric code.
- **Blazing Fast** — Compiles to native machine code for near-C performance. The compiler uses a temporary C backend and removes the generated source after compilation.
- **Statically Typed** — Type errors are caught at compile time before execution.
- **Simple & Clean Syntax** — Readable syntax with optional semicolons and zero clutter. Spend time on the math, not the boilerplate.
- **Cross-Platform** — Native executable support for **Linux** and **Windows**.
- **C Interoperability** — Call linked C functions with typed `extern` declarations, or use `C_call` for a raw statement when needed.

Modules and the `qpm` package manager are supported, but Quill's primary focus is **fast numeric code** and a clear path to high-performance native compilation — not a large packaging ecosystem.

---

## Installation

Download the pre-compiled binary for your platform from the [Releases](https://github.com/omo5454/Quill/releases) page.

### Windows
Install the quill-windows[32 or 64].exe and run it in powershell.

### Linux
Make the binary executable and run:
```bash
chmod +x quill-linux
./quill-linux myfile.qsc
```

> **Note on macOS:** Support for macOS is currently paused to ensure maximum stability on core Linux and Windows platforms.

---

## Usage

```bash
# Compile to a native binary (default)
quill-[os] myfile.qsc

# Emit C only; do not compile
quill-[os] --emit-c myfile.qsc -o output.c
```

---

## Language Tour

### Comments

`#` runs to the end of the line. There's no block-comment syntax.

```quill
# this is a comment
let x: number = 5; # so is this
```

### Variables

Variables are declared with `let`, `mut`, or `const`. Type annotations are supported.

```quill
let x: number = 5;
let name: string = "Alice";
let pi: double = 3.14;
let active: bool = true;
let ch: char = 'A';
```

> **Note:** `let`, `mut`, and `const` are currently interchangeable — Quill does not yet enforce immutability. A `const` can be reassigned without error. Treat the keyword as documentation of intent for now, not a guarantee.

### Printing

```quill
say "Hello, world!";
printf("Hello " + name);
```

Any type can be concatenated onto a string with `+` — Quill converts it automatically.

### Arithmetic & Incrementing

```quill
let sum: number = 10 + 5;
let product: number = 6 * 7;

let i: number = 0;
i++;   # increment
i--;   # decrement
```

`++`/`--` are statements, not expressions — `arr[i++]` isn't valid; increment on its own line instead.

### Control Flow

Conditions do not require parentheses.

```quill
let score: number = 75;

if score >= 90 {
    say "Grade: A";
} else if score >= 75 {
    say "Grade: B";
} else {
    say "Grade: F";
}

let x: number = 0;
while x < 5 {
    printf("x: " + x);
    x++;
}
```

> **Note:** `elif` is reserved but not yet wired into the parser — use `else if` (two words) for else-if chains.

### Arrays and Lists

Fixed-size arrays and growable `Vec` values are supported. Growable arrays can be passed to and returned from functions; mutations through a parameter affect the caller's list.

```quill
let stack: number[200];
stack[0] = 42;
say stack[0];

let mut values: Vec<number> = [2, 7];
push(values, 11);
say values[2];

fn increment_first(items: Vec<number>) -> Vec<number> {
    items[0] = items[0] + 1;
    return items;
}
```

`Vec<number>` is equivalent to `number[]`; `double`, `string`, and `bool` vectors are also supported. Use `len`, `push`, and `pop` with growable arrays.

String equality compares text contents. Strings can be indexed to read character codes, which supports parsers and digit-oriented algorithms without C interop. `toInt` and `toDouble` provide explicit numeric conversions.

Structs are value types and can be passed to and returned from functions. String helpers include `charAt`, `toChar`, `substring`, `contains`, `startsWith`, `endsWith`, and `indexOf`; pure-Quill utilities live in `stdlib/string/string.qsc`.

### Functions

Functions must be declared before they are called. Rust-style `fn` and `->` syntax is supported; the older `func` and `: ReturnType` spelling remains accepted. Return types are optional and inferred when omitted. Two functions can't share a name anywhere in the whole program, including across imports.

```quill
fn add(a: number, b: number) -> number {
    return a + b;
}

func greet(name: string): string {
    return "Hello, " + name;
}

let result: number = add(3, 4);
say greet("Alice");
```

### Standard Library

Quill's own stdlib is intentionally small and math-leaning:

```quill
len("hello")        # -> 5
toString(42)         # -> "42"
input(someNumberVar) # reads a number from stdin into an existing variable
input(someCharVar)   # reads a character (skips leading whitespace)
input(someStringVar) # reads a full line into a string
```

`s[i]` indexes a string, returning that character's code as a `number` (the same way C represents `char`).

Math helpers live under `stdlib/math/` (sqrt, pow, floor, factorial, fib, …). Import them when you need them.

Random helpers are available from the standard library without C declarations:

```quill
import "stdlib/random/random.qsc";
say random_range(1, 6);
```

### Calling C Directly

For anything else, `C_call("...")` splices raw C directly into your program, at the exact spot it's written:

```quill
C_call("printf(\"%s\\n\", \"raw C, verbatim\");");
```

`C_top` has been removed. Declare Quill globals directly at file scope:

```quill
let mut total: number = 0;

fn add_to_total(value: number) -> number {
    total = total + value;
    return total;
}
```

**Important:** any backslash you want to survive into the generated C must be doubled. Quill's own string lexer un-escapes `\n` into a real newline character *before* `C_call` ever sees it — write `\\n` to get a literal `\n` in the output.

### `extern` Functions

Tell Quill about a C function's signature (implemented by a library you've linked, or already available via the default headers) so it's typed correctly everywhere you use it — without emitting any C definition of your own:

```quill
extern func sqrt(x: double): double;

say sqrt(64);              # correctly formatted as a double
say "root: " + sqrt(64);
```

Skipping this for a `double`/`bool`-returning C function will silently default to treating it as a `number`, which gcc will flag as a format mismatch.

### Modules & Imports

`import "path.qsc";` splices another file's entire contents — functions *and* top-level code — in, exactly where the `import` line sits (much like C's `#include`). Relative paths resolve against the importing file's own directory. Circular imports are caught with a clear error.

```quill
# mathlib.qsc
func square(x: number): number {
    return x * x;
}

# main.qsc
import "mathlib.qsc";
say square(5);
```

**Installing shared modules from GitHub:** use `qpm`, Quill's package manager, to pull a module down locally, then `import` it like any other file:

```bash
qpm add username/repo
```

```quill
import "qpm_modules/repo/lib.qsc";
```

(Adjust the path to match wherever `qpm` places the module on your machine.)

Modules are available for organizing larger programs; they are not the main focus of the language.

---

## Type System

Quill runs static type checking before generating native code.

| Type | Example |
| --- | --- |
| `number` | `42` |
| `double` | `3.14` |
| `string` | `"hello"` |
| `bool` | `true` / `false` |
| `char` | `'A'` |
| `void` | Functions with no return value |
| `number[N]` / `double[N]` / `string[N]` / `bool[N]` | `let stack: number[200];` |

---

## Building from Source

Requires **C++17** or higher.

### Windows (PowerShell)
Currently has no helper file due to complexity.

### Linux

```bash
git clone https://github.com/omo5454/Quill.git
cd Quill
./build.sh
```

---

## Project Structure

```
Quill/
├── src/
│   └── core/
│       ├── ast/          # AST node definitions
│       ├── lexer/        # Tokenizer
│       ├── parser/       # AST parser
│       ├── transpiler/   # Quill → C pipeline (current backend) + CLI
│       └── typechecker/  # Static type checker
├── stdlib/               # Math, stdio, and other small libraries
├── bin/                  # Output directory for compiled binaries
├── build.sh              # Linux build script
└── Readme.md
```

---

## Known Limitations

- `let`/`mut`/`const` don't enforce immutability yet.
- `elif` is reserved but not parsed — use `else if`.
- No closures, function values, or higher-order functions — a function can only be called by name where it's written.
- No object/class system yet; string-keyed maps are supported.
- `import` only works at the top level of a file; writing it inside a function or block is silently ignored.

---

## Roadmap

### Language & libraries
* [x] Fixed-size arrays (`number`/`double`/`string`/`bool`, indexed read/write)
* [x] `char` type and character literals
* [x] String, number, and char `input()`
* [x] File imports (`import "path.qsc";`)
* [x] `extern` function declarations for typed C interop
* [x] Raw C statement interop (`C_call`)
* [x] Dynamic arrays with `Vec<T>`, function parameters/returns, `push`, and `pop`
* [x] String-keyed hash maps
* [ ] Objects / classes
* [ ] Extended string utilities (`split`, `trim`, `replace`)
* [ ] Richer math stdlib (vectors, matrices, more numeric helpers)
* [ ] Native File I/O (raw `<stdio.h>` functions are already callable via `extern`)
* [ ] Structured error handling (`try` / `catch`)

### Compiler backend
* [x] C++ transpiler pipeline (current: Quill → C → gcc)
* [ ] **LLVM backend** — compile Quill directly to LLVM IR / native machine code, dropping the C intermediate step for tighter optimization and a simpler runtime story
* [ ] Incremental improvements to the current C backend until the LLVM path is ready

### Tooling
* [x] Package manager (`qpm`) for optional GitHub-based modules
* [ ] Better diagnostics and error messages
* [ ] Formatter / basic IDE support

---

## License

Distributed under the **MIT License**.
