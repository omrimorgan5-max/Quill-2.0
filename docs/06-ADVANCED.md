# Advanced Topics

This guide covers advanced features and patterns for experienced Quill programmers.

## Calling C Functions

### Using `extern`

Declare C function signatures to call them with full type safety:

```quill
extern func sqrt(x: double): double;
extern func abs(x: number): number;
extern func strlen(s: string): number;

let result: double = sqrt(16.0);  # Type-safe
say strlen("hello");               # Type-safe
```

### Standard C Libraries

Quill automatically includes these headers in every generated program:

- `<stdio.h>` — I/O functions
- `<string.h>` — String functions
- `<ctype.h>` — Character functions
- `<stdlib.h>` — Utility functions

This means you can use common C functions:

```quill
extern func strcmp(a: string, b: string): number;
extern func isdigit(c: number): bool;
extern func atoi(s: string): number;
extern func rand(): number;
```

### Type Mismatch Issues

If you don't declare a C function with `extern`, the compiler defaults to assuming it returns `number`. This can cause format mismatches:

```quill
# BAD: sqrt not declared, defaults to number return
say sqrt(16);  # Compiler treats result as number, but it's actually double

# GOOD: sqrt properly declared with extern
extern func sqrt(x: double): double;
say sqrt(16);  # Correctly typed
```

## Embedding Raw C

### C_call

Splice raw C at the exact location in your code:

```quill
let x: number = 5;
C_call("printf(\"x is %d\\n\", x);");
```

Important notes about escaping:
- Quill's string lexer un-escapes `\n` before `C_call` sees it
- Write `\\n` to get a literal `\n` in the C output
- Every backslash that needs to survive must be doubled

```quill
# WRONG: \n gets unescaped
C_call("printf(\"hello\n\");");     # Results in actual newline in C

# CORRECT: \\n stays as literal \n
C_call("printf(\"hello\\n\");");    # Results in \n in C output
```

### Global Variables

```quill
let mut global_counter: number = 0;

fn next_counter() -> number {
    global_counter++;
    return global_counter;
}
```

Top-level variables are file-scope Quill declarations. `C_top` is not supported.

## Working with Structs

Quill supports struct declarations directly:

```quill
struct Point {
    x: number,
    y: number
}
```

## Module Organization

### File Structure

Organize larger projects with modules:

```
project/
├── main.qsc
├── math/
│   ├── geometry.qsc
│   └── algebra.qsc
└── utils/
    ├── string.qsc
    └── array.qsc
```

### Main Program

```quill
# main.qsc
import "math/geometry.qsc";
import "math/algebra.qsc";
import "utils/string.qsc";

# Your main code here
say "Program starts";
```

### Math Module

```quill
# math/geometry.qsc
func distance(x1: double, y1: double, x2: double, y2: double): double {
    let dx: double = x2 - x1;
    let dy: double = y2 - y1;
    return (dx * dx + dy * dy);  # Note: returns squared distance for efficiency
}
```

### Using QPM

For larger projects, use Quill's package manager:

```bash
# Install a package
qpm add username/mylib

# Use it in your code
import "qpm_modules/mylib/lib.qsc";
```

## Performance Patterns

### Minimize Function Call Overhead

For performance-critical code, inline simple operations:

```quill
# Instead of calling a function repeatedly
let result: number = 0;
let i: number = 0;
while i < 1000000 {
    result = result + (i * i);  # Inline the operation
    i++;
}
```

### Use Fixed-Size Arrays

Quill's fixed-size arrays are fast:

```quill
let arr: number[10000];  # Fast, pre-allocated
```

### Prefer `number` for Integers

`number` is typically more efficient than `double` for integer operations:

```quill
let count: number = 0;    # Efficient
let sum: number = 0;      # Efficient
```

## Debugging

### Print Debugging

Use `say` and `printf` to trace execution:

```quill
say "Entering loop";
let i: number = 0;
while i < 10 {
    printf("i = " + i);
    i++;
}
say "Exiting loop";
```

### Inspecting Generated C

Generate C without compiling to see what Quill produces:

```bash
./quill-linux myfile.qsc -o myfile.c
cat myfile.c  # View the generated C code
```

This is useful for understanding performance characteristics or debugging unexpected behavior.

### Using C Debugger

Since Quill compiles to C and then machine code, you can use standard C debuggers:

```bash
# Emit C for inspection, then compile it with debugging symbols
quill --emit-c myfile.qsc -o myfile.c
gcc -g myfile.c -o myfile_debug
gdb ./myfile_debug
```

## Limitations & Workarounds

### Growable Arrays in Functions

Growable arrays can be passed to functions and returned:

```quill
fn update(items: Vec<number>) -> Vec<number> {
    push(items, 10);
    items[0] = items[0] + 1;
    return items;
}

let mut values: Vec<number> = [1, 2];
let updated: Vec<number> = update(values);
```

### Dynamic Arrays

Use `Vec<T>` or `T[]` for growable arrays. `push`, `pop`, and `len` are built in.

```quill
let mut values: Vec<number> = [];
push(values, 5);
say pop(values);
```

Hash maps are available for string keys through `map[string, T]` and the `map_has`, `map_del`, and `map_len` helpers.

### Two Functions Cannot Share a Name

All functions must have unique names across the entire program, including imported modules.

## C Interoperability Patterns

### Wrapping C Libraries

```quill
# Use C libraries with extern declarations
extern func malloc(size: number): number;
extern func free(ptr: number);

# For <string.h> functions
extern func strcpy(dest: string, src: string): string;
extern func strcat(dest: string, src: string): string;
```

`C_top` is removed. Use Quill globals, structs, and library functions for program-level declarations.

## Optimization Tips

1. **Minimize string concatenation in loops** — It's slower than accumulating in C
2. **Use `C_call` for performance-critical sections** — Raw C can be faster
3. **Avoid deep recursion** — Large call stacks can overflow
4. **Precompute values** — Don't recalculate in loops
5. **Use `double` only when needed** — `number` is faster for integers

## Next Steps

- [Language Guide](02-LANGUAGE-GUIDE.md)
- [Type System](03-TYPE-SYSTEM.md)
- [Code Examples](05-EXAMPLES.md)
