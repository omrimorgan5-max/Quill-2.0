# Language Guide

This guide covers Quill's syntax and language features in detail.

## Comments

Use `#` to comment to the end of the line. There are no block comments.

```quill
# This is a comment
let x: number = 5; # So is this
```

## Variables

Variables are declared with `let`, `mut`, or `const`. Type annotations are optional but recommended for clarity.

```quill
let x: number = 5;
let name: string = "Alice";
let pi: double = 3.14;
let active: bool = true;
let ch: char = 'A';
```

> **Note:** `let`, `mut`, and `const` are currently interchangeable — Quill does not yet enforce immutability. Treat the keyword as documentation of intent for now.

## Printing Output

Use `say` or `printf` to print to the console:

```quill
say "Hello, world!";
printf("Value: " + x);
```

Any type can be concatenated with `+` — Quill automatically converts it to a string.

```quill
let count: number = 42;
printf("Count: " + count);  # Output: Count: 42
```

## Operators

### Arithmetic

```quill
let sum: number = 10 + 5;      # Addition: 15
let diff: number = 10 - 5;     # Subtraction: 5
let product: number = 6 * 7;   # Multiplication: 42
let quotient: number = 20 / 4; # Division: 5
let remainder: number = 20 % 3; # Modulo: 2
```

### Comparison

```quill
x == y    # Equal
x != y    # Not equal
x < y     # Less than
x > y     # Greater than
x <= y    # Less than or equal
x >= y    # Greater than or equal
```

### Logical

```quill
x && y    # Logical AND
x || y    # Logical OR
!x        # Logical NOT
```

### Increment/Decrement

`++` and `--` are statements, not expressions. Use them on their own line:

```quill
let i: number = 0;
i++;      # Increment
i--;      # Decrement
```

> **Note:** You cannot use `i++` as part of an expression like `arr[i++]`. The compiler will reject it.

## Control Flow

### If/Else

Conditions do not require parentheses:

```quill
let score: number = 75;

if score >= 90 {
    say "Grade: A";
} else if score >= 75 {
    say "Grade: B";
} else {
    say "Grade: F";
}
```

> **Note:** Use `else if` (two words), not `elif`. The keyword `elif` is reserved but not yet fully implemented.

### While Loops

```quill
let x: number = 0;
while x < 5 {
    printf("x: " + x);
    x++;
}

for value in values {
    if value < 0 {
        continue;
    }
    if value == 10 {
        break;
    }
}
```

Use `break` to leave the nearest loop and `continue` to move to its next iteration.

## Arrays and Lists

Fixed-size arrays use a literal size. Growable arrays use `Vec<T>` (or the equivalent `T[]`) and can be passed to and returned from functions.

```quill
let stack: number[200];
stack[0] = 42;
say stack[0];

let mut values: Vec<number> = [2, 7];
push(values, 11);
say values[2];
```

All of these array types work:
- `number[N]`
- `Vec<number>` / `number[]`
- `Vec<double>` / `double[]`
- `Vec<string>` / `string[]`
- `Vec<bool>` / `bool[]`

## Functions

Functions use Rust-style `fn` and `->` syntax. The older `func` keyword and colon return annotation remain supported. Return types are inferred when omitted.

```quill
fn add(a: number, b: number) -> number {
    return a + b;
}

func greet(name: string): string {
    return "Hello, " + name;
}

func printMessage(msg: string) {
    say msg;
}
```

### Calling Functions

Functions must be declared before they are called:

```quill
let result: number = add(3, 4);
say greet("Alice");
printMessage("Hi there");
```

> **Important:** Two functions cannot share the same name anywhere in the program, even across imports.

## Struct Values

Quill structs can be passed to and returned from functions by value:

```quill
struct Point {
    x: number,
    y: number
}

fn translate(point: Point, dx: number, dy: number) -> Point {
    point.x = point.x + dx;
    point.y = point.y + dy;
    return point;
}
```

The function receives a value copy; return the updated value when the caller needs it.

## Strings

Strings are enclosed in double quotes. You can access individual characters by index:

```quill
let s: string = "hello";
let ch: number = s[0];  # 'h' as a character code
```

String functions:

```quill
len("hello")           # -> 5
toString(42)           # -> "42"
charAt("hello", 0)     # -> 'h'
substring("hello", 1, 4) # -> "ell"
input(someStringVar)   # reads a full line into a string
```

When you index a string with `[i]`, you get that character's code as a `number` (the same way C represents `char`).

## Characters

Characters are single characters enclosed in single quotes:

```quill
let ch: char = 'A';
input(someCharVar)  # reads a single character
```

## Reading Input

Use `input()` to read from stdin:

```quill
let num: number;
input(num);      # reads a number

let ch: char;
input(ch);       # reads a character (skips leading whitespace)

let line: string;
input(line);     # reads a full line
```

## Standard C Functions

Quill automatically includes these C headers for every generated file:
- `<stdio.h>`
- `<string.h>`
- `<ctype.h>`
- `<stdlib.h>`

This means functions like `strcmp`, `strlen`, `isdigit`, `toupper`, and `atoi` are available by simply declaring them with `extern`:

```quill
extern func strlen(s: string): number;
extern func strcmp(a: string, b: string): number;
extern func toupper(c: number): number;

say strlen("hello");       # -> 5
say strcmp("a", "b");      # -> negative
```

## Calling C Directly

### C_call

Splice raw C code directly into your program at that exact spot:

```quill
C_call("printf(\"%s\\n\", \"raw C, verbatim\");");
```

> **Important:** Any backslash you want to survive into the generated C must be doubled. Quill's string lexer un-escapes `\n` before `C_call` sees it — write `\\n` to get a literal `\n` in the C output.

## Global Variables

Top-level Quill variables are stored at file scope and initialized before top-level statements run. Functions can read and update them directly:

```quill
let mut global_count: number = 0;

fn increment_global() -> number {
    global_count++;
    return global_count;
}
```

`C_top` is not part of the language.

## Extern Functions

Declare C function signatures so Quill knows their types:

```quill
extern func sqrt(x: double): double;
extern func pow(x: double, y: double): double;

say sqrt(64);              # correctly typed as double
say "root: " + sqrt(64);
```

Skipping `extern` for a `double` or `bool` returning C function will silently default to `number`, which might cause format mismatches.

## Modules & Imports

`import` splices another file's contents — both functions and top-level code — at that exact spot:

```quill
# mathlib.qsc
func square(x: number): number {
    return x * x;
}

# main.qsc
import "mathlib.qsc";
say square(5);
```

Relative paths resolve against the importing file's directory. Circular imports are caught with a clear error.

### Importing from qpm (Package Manager)

Install packages with `qpm` and import them:

```bash
qpm add username/repo
```

```quill
import "qpm_modules/repo/lib.qsc";
```

## Scope & Visibility

Variables are function-local by default. There is no global scope in Quill (yet).

All top-level code (outside of function definitions) runs in a default `main()` function automatically generated by the compiler.

## Semicolons

Semicolons are optional. You can include them for clarity or omit them — both styles work.

```quill
let x: number = 5;
let y: number = 10

if x < y
{
    say "x is less"
}
```

## Next Steps

- [Type System Reference](03-TYPE-SYSTEM.md)
- [Standard Library Functions](04-STDLIB.md)
- [Code Examples](05-EXAMPLES.md)
