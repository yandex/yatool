# polexpr — Polish Notation Expression Tree Library

**polexpr** is a library for building, storing, evaluating, and printing generic expression trees using [Polish (prefix) notation](https://en.wikipedia.org/wiki/Polish_notation).

Expressions are stored as flat arrays of typed nodes — no heap-allocated tree nodes, no pointers. The format is compact, cache-friendly, and directly serializable.

---

## Key Features

- **Compact flat representation.** The entire expression tree is a flat array of typed nodes. Each node size is 32 bits.
- **Binary serialization.** `TExpression` supports `Y_SAVELOAD_DEFINE` based serizlization out of the box.
- **Evaluation via callback.** A single template function `Evaluate<TValue>()` traverses the expression and dispatches to a user-supplied callable for constants, variables, and function calls.
- **Human-readable printing.** `Print()` renders the expression in conventional infix-like `f(a, b)` notation with a user-supplied name resolver.
- **Subexpression deduplication (backrefs).** Any node or completed subexpression can be marked as referenceable and re-used later in the same expression without duplicating its computation.
- **Fixed-arity and variadic functions.** Arity is encoded directly into `TFuncId` at build time. `TVariadicCallBuilder` supports functions whose argument count is not known upfront.

---

## Expression Format

Nodes are stored in **prefix (Polish) order**: a function node is followed immediately by its arguments, each of which may itself be a sub-tree. During evaluation the library maintains an argument stack and a call stack, reducing each completed call to a single value.

### Node Types (`TExpression::TNode::EType`)

| Type       | Description |
|------------|-------------|
| `Constant` | References a value in a constant pool via `TConstId` |
| `Variable` | References a runtime variable via `EVarId` |
| `Function` | Calls a function identified by `TFuncId` (arity encoded in the id) |
| `Backref`  | Re-uses the cached result of a previously evaluated referenced node |

Each node is packed into a `ui32`:

```
bits [31:30] — EType (2 bits)
bit  [29]    — Referenced flag (marks node as reusable via backref)
bits [28:0]  — Payload (constant/var/func index, or function arity+index)
```

---

## Building Expressions

The library does **not** provide parsers. Expressions are assembled programmatically via `TExpression::Append()` overloads:

```cpp
NPolexpr::TExpression expr;

// Append a constant node
expr.Append(TConstId{0, 42});

// Append a variable node
expr.Append(myVar);

// Append a function node (must be followed by exactly arity argument sub-trees)
NPolexpr::TFuncId mulFunc{2, 0};  // binary function at index 0
expr.Append(mulFunc);
expr.Append(TConstId{0, 3});      // first argument
expr.Append(myVar);               // second argument
// encodes:  Mul(3, $myVar)
```

---

## Evaluation

`Evaluate<TValue>()` is a template function in [`evaluate.h`](evaluate.h). It walks the expression, accumulates argument values on a stack, and dispatches to a user-provided callable:

```cpp
// The eval callable must handle three overloads:
//   TValue operator()(TConstId)                         — resolve a constant
//   TValue operator()(EVarId)                           — resolve a variable
//   TValue operator()(TFuncId, std::span<const TValue>) — call a function

auto result = NPolexpr::Evaluate<int>(expr, [&](auto id, auto... args) {
    if constexpr (std::is_same_v<decltype(id), NPolexpr::TConstId>)
        return constPool[id.GetIdx()];
    else if constexpr (std::is_same_v<decltype(id), NPolexpr::EVarId>)
        return vars.at(id);
    else  // TFuncId
        return dispatchFunc(id, args...);
});
```

---

## Printing

`Print()` in [`evaluate.h`](evaluate.h) renders an expression in human-readable form. The caller supplies a `getName` callable that maps each id type to `std::string_view`:

```cpp
NPolexpr::Print(Cout, expr, [&](auto id) -> std::string_view {
    if constexpr (std::is_same_v<decltype(id), NPolexpr::TConstId>)
        return constNames[id.GetIdx()];
    else if constexpr (std::is_same_v<decltype(id), NPolexpr::EVarId>)
        return varNames[static_cast<ui32>(id)];
    else  // TFuncId
        return funcNames[id.GetIdx()];
});
```

Example outputs:

```
Mul(3, $THE_VAL)
Quote(Concat(Hello , $NAME))
Concat([$0 = hello], $NAME, $0)   // $0 is a backref (see below)
```

---

## Subexpression Deduplication (Backrefs)

Backrefs allow the result of a sub-tree to be used multiple times without re-evaluating or re-storing it. They are managed through a `TRefsRegistry` scope object and the ref-accepting `Append` overloads.

```cpp
NPolexpr::TExpression expr;
NPolexpr::TExpression::TRefsRegistry refs;

expr.Append(concatFunc);
const auto subRef = expr.Append(refs, twiceFunc);   // start referencing Twice(
expr.Append(mulFunc);                                //   Mul(
expr.Append(myVar3);                                 //     $VAR3,
expr.Append(myConst4);                               //     4))
const auto ref = subRef.Finish();                    // close the referenceable range

expr.Append(refs, ref);                              // $0  (reuse Twice(Mul($VAR3, 4)))
```

Printed form: `Concat([$0 = Twice(Mul($VAR3, 4))], $0)`

During evaluation referenced nodes/subexpressions are cached in an internal store stack and retrieved without re-computation on each backref.

---

## Variadic Functions

When the number of arguments is not known at the time the function node is written, use `TVariadicCallBuilder` from [`variadic_builder.h`](variadic_builder.h). It reserves the function node with a placeholder arity, forwards all `Append` calls to the underlying expression while counting arguments, and patches the real arity on `Build()`.

```cpp
NPolexpr::TExpression expr;
NPolexpr::TVariadicCallBuilder builder{expr, concatFunc};

builder.Append(helloConst);
builder.Append(nameVar);
builder.Append(exclConst);

builder.Build<ui32>();  // template arg is the enum/integral type of func indices
// Result: Concat('Hello ', $NAME, '!')  — arity patched to 3
```

---

## What the Library Does NOT Provide

- **No expression parsers.** Text-to-expression conversion is the caller's responsibility.
- **No built-in value types or function registry.** The caller defines constants, variables, and functions and maps them to integer IDs.
