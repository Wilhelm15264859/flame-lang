# FLAME LANGUAGE
Flame is a natively compiled systems programming language.
Its goal is to provide the power and perfomance of the C lnaguage, but in acleaner and simpler way.
In the feuture, exception policies, OOP, native ownership and mamory safety will be implemented.

Currently, the file is compiled only into .bc (LLVM Byte Code), 
you need to manually compile it into an object file and link it to make an executable file

## Dependecies:
libc, llvm-config, llc

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

- Basic arithmetic (like in C)
