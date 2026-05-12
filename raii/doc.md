# RAII — Resource Acquisition Is Initialization

RAII ties the **lifetime of a resource** to the **lifetime of a C++ object**.  
The resource is acquired in the constructor and released in the destructor.

This works because C++ guarantees that destructors are called deterministically
when an object leaves scope — even when an exception is thrown.  
You never have to remember to free/close/unlock; the type enforces it.

---

## The invariant

```
Constructor  →  acquire resource   (throw on failure; destructor won't run)
Destructor   →  release resource   (unconditionally, always runs)
```

Once the constructor completes successfully, the destructor is guaranteed to run.
Resource leaks become structurally impossible.

---

## Rule of Five (or Zero)

When a class owns a resource it must define (or explicitly delete) all five
special member functions, because the compiler-generated defaults do the wrong thing:

| Function         | What it must do for the resource               |
|------------------|------------------------------------------------|
| Destructor       | Release it                                     |
| Copy constructor | Deep-copy it, or `= delete`                    |
| Copy assignment  | Release old, deep-copy new, or `= delete`      |
| Move constructor | Transfer ownership; null out source            |
| Move assignment  | Release old, transfer from source, null source |

Omitting any of them while defining another leads to double-frees or leaks.

**Rule of Zero** is the better goal: compose your class from existing RAII types
(`unique_ptr`, `string`, `fstream`) so the compiler-generated defaults are correct
and you need none of the five.

---

## Standard library RAII wrappers — prefer these

| Resource          | RAII type                                   |
|-------------------|---------------------------------------------|
| Heap memory (1:1) | `std::unique_ptr<T>`                        |
| Heap memory (shared) | `std::shared_ptr<T>`                     |
| Mutex             | `std::lock_guard<M>` / `std::unique_lock<M>`|
| File              | `std::fstream`                              |

---

## Examples

| File                    | Covers                                                    |
|-------------------------|-----------------------------------------------------------|
| `01_memory.cpp`         | Heap memory — raw leak, hand-rolled wrapper, `unique_ptr` |
| `02_file.cpp`           | File handles — `FILE*` wrapper, `std::fstream`            |
| `03_move.cpp`           | Rule of Five — why ownership transfer requires move       |
| `04_lock.cpp`           | Mutexes — `lock_guard` vs `unique_lock`, deadlock on exception |
| `05_custom_deleter.cpp` | `unique_ptr` custom deleters — wrapping C APIs            |
| `06_rule_of_zero.cpp`   | Rule of Zero — composing from stdlib types, no boilerplate |
