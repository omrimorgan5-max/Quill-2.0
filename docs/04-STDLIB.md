# Standard Library

Quill's standard library is intentionally small and focused on mathematical and string operations. This guide covers the built-in functions available.

## String Functions

### len(s: string): number

Returns the length of a string in characters.

```quill
len("hello")    # → 5
len("")         # → 0
len("Quill")    # → 5
```

### toString(n: number): string

Converts a number to its string representation.

```quill
toString(42)     # → "42"
toString(-100)   # → "-100"
toString(0)      # → "0"
```

`toInt(double)` truncates toward zero, and `toDouble(number)` converts an integer to floating point.

### Character and substring operations

```quill
toChar(65)                    # 'A'
charAt("Quill", 0)            # 'Q'
substring("Quill", 1, 4)     # "uil"; end index is exclusive
contains("Quill", "ui")       # true
startsWith("Quill", "Qui")    # true
endsWith("Quill", "ill")      # true
indexOf("Quill", "ui")        # 1; returns -1 when absent
```

The pure-Quill utilities in `stdlib/string/string.qsc` include `reverse_string`, `trim_ascii`, `is_ascii_digit`, and `is_ascii_letter`.

### input(variable)

Reads from stdin into an existing variable.

```quill
let num: number;
input(num);      # reads a number

let ch: char;
input(ch);       # reads a character (skips leading whitespace)

let line: string;
input(line);     # reads a full line
```

## Math Library

Quill includes a growing set of math functions implemented in Quill. Import them from the standard library:

```quill
import "../stdlib/math/math.qsc";
```

### Basic Math Functions

```quill
abs_quill(x)          # Absolute value
floor_quill(x)        # Floor (largest integer ≤ x)
ceil_quill(x)         # Ceiling (smallest integer ≥ x)
round_quill(x)        # Round to nearest integer
trunc_quill(x)        # Truncate (remove decimal)
```

### Power & Roots

```quill
sqrt_quill(x)         # Square root
pow_quill(x, y)       # x raised to power y
cubrt_quill(x)        # Cube root
hypot_quill(x, y)     # Hypotenuse (√(x² + y²))
```

### Exponential & Logarithmic

```quill
exp_quill(x)          # e raised to power x
```

### Floating-Point Utilities

```quill
copysign_quill(x, y)  # Copy sign of y to x
int_ceil_quill(a, b)  # Ceiling division (⌈a/b⌉)
```

### Floating-Point Checks

```quill
is_inf(x)             # Check if x is infinity
is_nan(x)             # Check if x is NaN (Not a Number)
```

## Random Numbers

Quill includes a random number module:

```quill
import "../stdlib/random/random.qsc";
```

> **Note:** The specific random functions available depend on the standard library implementation. Consult `stdlib/random.qsc` for details.

## Default C Headers

Every generated Quill program automatically includes these C headers:

- `<stdio.h>` — Standard I/O
- `<string.h>` — String operations
- `<ctype.h>` — Character classification
- `<stdlib.h>` — Utility functions

This means you can declare and use standard C functions directly:

```quill
extern func strlen(s: string): number;
extern func strcmp(a: string, b: string): number;
extern func toupper(c: number): number;
extern func tolower(c: number): number;
extern func isdigit(c: number): bool;
extern func isalpha(c: number): bool;
extern func atoi(s: string): number;
extern func rand(): number;

say strlen("hello");       # → 5
say strcmp("apple", "banana");  # → negative
say toupper('a');          # → 65 (ASCII code of 'A')
```

## Using C Functions

### Extern Declaration

Declare C function signatures to use them:

```quill
extern func strlen(s: string): number;
```

### Examples

```quill
# String comparison
extern func strcmp(a: string, b: string): number;
if strcmp(name, "Alice") == 0 {
    say "Hello Alice!";
}

# Character classification
extern func isdigit(c: number): bool;
let ch: char = '5';
if isdigit(ch) {
    say "It's a digit";
}

# String to integer
extern func atoi(s: string): number;
let num: number = atoi("42");
```

## Direct C Code

For functions not in the standard library, use `C_call()` to embed raw C:

```quill
C_call("printf(\"Hello from C\\n\");");
```

Top-level Quill declarations are emitted at file scope. Use a Quill global instead of embedding a raw C global.

## Module System

### Importing Local Modules

Import other `.qsc` files:

```quill
import "mathlib.qsc";
import "../shared/utils.qsc";
```

### Using qpm Packages

Install community packages with the Quill Package Manager:

```bash
qpm add username/repo
```

Then import them:

```quill
import "qpm_modules/repo/lib.qsc";
```

## Built-in Output Functions

### say

Prints a value with a newline:

```quill
say "Hello, world!";
say 42;
say true;
```

### printf

Prints formatted output (similar to C's printf, but concatenation with `+` is simpler):

```quill
printf("Value: " + x);
printf("Sum: " + (a + b));
```

## Best Practices

1. **Import only what you need** — Import specific modules like `stdlib/math.qsc` to keep compilation fast
2. **Use `extern` for C functions** — Declare C function signatures so Quill can type-check them
3. **Check function signatures** — Look at `stdlib/` files to understand available functions
4. **Use `C_call()` sparingly** — It bypasses type safety; use it only when necessary

## Next Steps

- [Language Guide](02-LANGUAGE-GUIDE.md)
- [Type System](03-TYPE-SYSTEM.md)
- [Code Examples](05-EXAMPLES.md)
- [stdlib/ Source](../stdlib/)
