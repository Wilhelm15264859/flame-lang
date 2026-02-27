# FLAME LANGUAGE
Flame is a natively compiled systems programming language.
Its goal is to provide the power and perfomance of the C lnaguage, but in acleaner and simpler way.
In the feuture, exception policies, OOP, native ownership and mamory safety will be implemented.

Currently, the file is compiled only into .bc (LLVM Byte Code), 
you need to manually compile it into an object file and link it to make an executable file

## Working with ASM:
The `x86` prefix is ​​used for this. For now, x86 is just a prefix for designation, 
but in the future, it will be used for insertion typing, so that ARM instructions cannot be 
inserted when compiling to x86. `$` is used for inserting variables.

## Number typing:
5 - int;
5s - short;
5l - long;

## Dependecies:
libc, llvm-config, llc, gcc

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
