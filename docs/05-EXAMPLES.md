# Code Examples

This document contains practical examples to help you learn Quill. Each example is runnable and demonstrates different language features.

## Hello World

The simplest program:

```quill
say "Hello, Quill!";
```

## Variables and Arithmetic

```quill
let x: number = 10;
let y: number = 20;
let sum: number = x + y;

say "x: " + x;
say "y: " + y;
say "sum: " + sum;
```

## Temperature Converter

Convert Fahrenheit to Celsius:

```quill
let fahrenheit: number = 100;
let celsius: double = (fahrenheit - 32) * 5.0 / 9.0;

printf("Fahrenheit: " + fahrenheit);
printf("Celsius: " + celsius);
```

## Grade Calculator

Determine a letter grade from a numeric score:

```quill
let score: number = 85;
let grade: string = "";

if score >= 90 {
    grade = "A";
} else if score >= 80 {
    grade = "B";
} else if score >= 70 {
    grade = "C";
} else if score >= 60 {
    grade = "D";
} else {
    grade = "F";
}

say "Score: " + score + " Grade: " + grade;
```

## Sum Numbers 1 to N

Using a loop:

```quill
let n: number = 100;
let sum: number = 0;
let i: number = 1;

while i <= n {
    sum = sum + i;
    i++;
}

say "Sum of 1 to " + n + " is " + sum;
```

## Factorial

Calculate N factorial recursively:

```quill
func factorial(n: number): number {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

let result: number = factorial(5);
say "5! = " + result;  # 120
```

## Fibonacci Sequence

Generate Fibonacci numbers:

```quill
func fibonacci(n: number): number {
    if n <= 1 {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

let i: number = 0;
while i < 10 {
    say "fib(" + i + ") = " + fibonacci(i);
    i++;
}
```

## Check if Prime

Determine if a number is prime:

```quill
func isPrime(n: number): bool {
    if n < 2 {
        return false;
    }
    let i: number = 2;
    while i * i <= n {
        if n % i == 0 {
            return false;
        }
        i++;
    }
    return true;
}

let num: number = 17;
if isPrime(num) {
    say num + " is prime";
} else {
    say num + " is not prime";
}
```

## Palindrome Checker

Check if a number is a palindrome:

```quill
func isPalindrome(x: number): bool {
    if x < 0 {
        return false;
    }
    let num: number = x;
    let reversed: number = 0;

    while num != 0 {
        reversed = reversed * 10 + num % 10;
        num = num / 10;
    }

    return x == reversed;
}

say isPalindrome(121);      # true
say isPalindrome(12321);    # true
say isPalindrome(123);      # false
```

## Array Operations

```quill
let arr: number[5];
arr[0] = 10;
arr[1] = 20;
arr[2] = 30;
arr[3] = 40;
arr[4] = 50;

# Sum array elements
let sum: number = 0;
let i: number = 0;
while i < 5 {
    sum = sum + arr[i];
    i++;
}

say "Sum: " + sum;
```

## Reverse a String

```quill
func reverseString(s: string): string {
    let len: number = len(s);
    let reversed: string = "";
    let i: number = len - 1;

    while i >= 0 {
        reversed = reversed + s[i];
        i--;
    }

    return reversed;
}

let original: string = "Hello";
let reversed: string = reverseString(original);

printf("Original: " + original);
printf("Reversed: " + reversed);
```

## Two Sum Problem

Find two numbers in an array that add up to a target (simplified version):

```quill
func twoSum(): string {
    let nums: number[6];
    nums[0] = 2;
    nums[1] = 7;
    nums[2] = 11;
    nums[3] = 15;
    nums[4] = 1;
    nums[5] = 8;

    let target: number = 26;
    let count: number = 6;

    let i: number = 0;
    while i < count {
        let j: number = i + 1;
        while j < count {
            if nums[i] + nums[j] == target {
                return nums[i] + " + " + nums[j] + " = " + target;
            }
            j++;
        }
        i++;
    }

    return "No pair found";
}

say twoSum();
```

## Rock, Paper, Scissors (Simplified)

```quill
func determineWinner(player: number, computer: number): string {
    # 0 = rock, 1 = paper, 2 = scissors
    
    if player == computer {
        return "It's a tie!";
    } else if player == 0 && computer == 2 {
        return "Rock beats scissors, you win!";
    } else if player == 1 && computer == 0 {
        return "Paper beats rock, you win!";
    } else if player == 2 && computer == 1 {
        return "Scissors beats paper, you win!";
    } else {
        return "Computer wins!";
    }
}

let player: number = 0;      # rock
let computer: number = 2;    # scissors

say determineWinner(player, computer);
```

## Using Math Functions

Requires importing the math library:

```quill
import "../stdlib/math.qsc";

let x: double = 16.0;
let sqrt_x: double = sqrt_quill(x);
printf("sqrt(" + x + ") = " + sqrt_x);

let y: double = 2.0;
let power: double = pow_quill(x, y);
printf("pow(" + x + ", " + y + ") = " + power);

let z: double = 3.7;
let floor_z: double = floor_quill(z);
let ceil_z: double = ceil_quill(z);
printf("floor(" + z + ") = " + floor_z);
printf("ceil(" + z + ") = " + ceil_z);
```

## String Manipulation

```quill
extern func strlen(s: string): number;
extern func strcmp(a: string, b: string): number;
extern func toupper(c: number): number;

let str1: string = "Hello";
let str2: string = "Hello";
let str3: string = "World";

say len(str1);                  # 5
say strcmp(str1, str2);         # 0 (equal)
say strcmp(str1, str3);         # negative (str1 < str3)

# Convert characters to uppercase (manually)
let i: number = 0;
while i < len(str1) {
    printf(toupper(str1[i]));
    i++;
}
```

## Global State

```quill
let mut timestamp: number = 0;

fn advance_timestamp(step: number) -> number {
    timestamp = timestamp + step;
    return timestamp;
}

say advance_timestamp(1);
```

## More Examples

See the `examples/` directory in the repository for additional complete programs:

- `examples/palindrome.qsc` — Check for palindromes
- `examples/reverse.qsc` — Reverse arrays or strings
- `examples/rps.qsc` — Rock-Paper-Scissors game
- `examples/twosum.qsc` — Find pairs that sum to a target
- `examples/leetcode_practice.qsc` — Quill-only solutions for 12 listed problems, including string arithmetic, vectors, and loop control
- `examples/struct_values.qsc` — Pass, update, and return a Quill struct value
- `examples/llvm_map.qsc` — String-keyed map operations on LLVM
- `examples/llvm_runtime.qsc` — LLVM file I/O and process arguments
- `examples/llvm_input.qsc` — Read input through LLVM
- `examples/llvm_smoke.qsc` — LLVM scalar, function, and control-flow smoke test
- `examples/bootstrap_lexer.qsc` — Run the Quill-written lexer/parser on source files
- `examples/bootstrap_program.qsc` — Numeric source accepted by the stage-0 Quill compiler
- `examples/bootstrap_function.qsc` — Exercise numeric function arguments and returns in stage 0
- `examples/exponent.qsc` — Calculate exponents
- `examples/math.example.qsc` — Demonstrate math library functions

Run any example:

```bash
./quill-linux examples/palindrome.qsc
./examples/palindrome
```

## Tips for Writing Quill Programs

1. **Start small** — Write simple programs and test them
2. **Use comments** — Document why, not what
3. **Declare types explicitly** — Makes code clearer
4. **Test edge cases** — Check zero, negative, and large values
5. **Use functions** — Break code into reusable pieces
6. **Leverage the standard library** — Use `sqrt`, `abs`, etc. when available
7. **Import only what you need** — Keep compilation fast

## Next Steps

- [Language Guide](02-LANGUAGE-GUIDE.md)
- [Type System](03-TYPE-SYSTEM.md)
- [Standard Library Reference](04-STDLIB.md)
