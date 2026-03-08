# FLAME LANGUAGE
Flame is a natively compiled systems programming language.
Its goal is to provide the power and perfomance of the C lnaguage, but in acleaner and simpler way.
In the feuture, exception policies, OOP, native ownership and mamory safety will be implemented.

Currently, the file is compiled only into .bc (LLVM Byte Code), 
you need to manually compile it into an object file and link it to make an executable file

## Working with ASM:
The `x86`/`avr`/... prefix is ​​used for this.`$` is used for inserting variables.
Full list:
"sparc64", "sparc", "bpf", "msp430", "avr", "wasm64", "wasm32", "ppc64", 
"ppc", "mips64", "mips", "riscv64", "riscv32",
"aarch64", "thumbeb", "thumb",  "armeb",  "arm", "i686", "i386", "x86_64"

## Number typing:
5 - int;
5s - short;
5l - long;

## Dependecies:
llvm, clang

## Exception structs:
Exception structures are structures that describe places where exceptions can occur, and describe checks or replacements for dangerous places.
The possible location is defined in `instruction {}`, and has its own formatting:

`%n` - any number;
`%k` - any keyword;
`%s` - any string;
`%i` - any identifier;
`Word`/`%"Word"` - any token with this lexeme;

`%n:varName` - becomes `var int varName = *that number*;`;
`%"(" ^ ")":varName` - becomes `var int varName = (*all in parens*);`;
`%"(" ^ ")"!k :varName` - the same, but with the assignment being terminated when the `%k` token is encountered;

`checker {}` inserts this code before the instruction where there is an exception.
`replace {}` replaces the entire instruction, you can write expressions with $ in it:

`$source` - inserts source instruction;
`$n` - inserts token with index `n` (first token in `instruction {}` have index 0);
`$ %TOK_INT "5"` - inserts custom token with type `TOK_TYPE` and lexeme `"5"`, full list:
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_OP,
    TOK_PAREN,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_EOF,
    TOK_ERROR,
    TOK_TYPE

### Exemple:
```Flame
exception DivByZero {
  var int op;

  instruction {
    %n / %:op
  }

  checker {
    if (op == 0) {
        var char *msg = "Error\n\0";
        Console.out(msg, 7l);
    }
  }
}

exception Repeat {
  var int count;

  instruction {
    repeat %n:count
  }

  replace {
    for (int i = 0; i < count; i++)
  }
}
```

## Currently aviable:
- Function declaration:

  ```Flame
  func int main() {
    ...
  }
  ```

- Return function:

  ```Flame
  func int main() {
    return 0;
  }
  ```

- Variable declaration:

  ```Flame
  var int a = 5;
  ```

- Array declaration:

  ```Flame
  var int a[10];
  ```

- while:

  ```Flame
  while (5 == 5) {
    someFunc();
  }
  ```

- if/else:

  ```Flame
  if (5 == 5) {
    some();
  }
  else {
    some();
  }
  ```

- Basic bitwise operations & arithmetic (like in C)

- class/struct:

  ```Flame
  class cls {
    var int a;
    func void b() {
      ...
    }
  }

  struct str {
    var int a;
    var int b;
  }
  ```

- Inline ASM:

  ```Flame
  x86 mov rax, 1;
  x86 mov rdi, 1;
  x86 mov rsi, $msg;
  x86 mov rdx, 5l;
  x86 syscall
  ```