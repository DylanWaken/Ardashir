# Arda Coding Conventions

These conventions apply to all project-owned C++ code. They are adapted from
Epic's C++ Coding Standard for Unreal Engine, with `Arda` used as the mandatory
project stem in type names and as the prefix for filenames.

## Naming

- Use U.S. English for names, comments, logs, and documentation.
- Use descriptive names and avoid abbreviations unless they are established
  domain terms, such as `GPU`, `D3D12`, or `NVRHI`.
- Use `PascalCase` without underscores for types, functions, variables,
  constants, and enum values.
- Use lowercase names for namespaces, for example `arda::backend`.
- Name types and variables with nouns.
- Name functions with verbs that describe their effect or result.

## Types

Every project-defined type must combine the applicable Unreal-style category
prefix with the `Arda` project stem:

- `FArda` for ordinary classes, structs, unions, and type aliases.
- `IArda` for abstract interfaces.
- `EArda` for enums.
- `TArda` for class and alias templates.

```cpp
class FArdaBackendDevice;
struct FArdaBackendConfiguration;
enum class EArdaBackendType;
using FArdaDeviceHandle = nvrhi::DeviceHandle;

template <typename ElementType>
class TArdaArray;
```

The category prefix comes first and `Arda` follows it immediately:

```cpp
// Correct
class FArdaRenderGraph;
enum class EArdaBackendType;

// Incorrect
class ArdaRenderGraph;
enum class ArdaBackendType;
class FRenderGraph;
```

This rule applies to classes, structs, enums, unions, aliases, interfaces, and
class or alias templates. It does not apply to fundamental types, template
parameters, or third-party types.

## Files

Every project-owned source filename must begin with `Arda` and use PascalCase.
Match implementation files to their primary declarations.

```text
ArdaBackendDevice.h
ArdaBackendDevice.cpp
ArdaBackendTests.cpp
ArdaBackendPch.h
```

Do not use generic names such as `BackendDevice.h` or snake_case filenames.
Files whose names are imposed by tools or platforms are exempt, including
`CMakeLists.txt`, `.gitignore`, package manifests, generated files, and
third-party files.

## Variables and Constants

- Use PascalCase for local variables, parameters, data members, and constants.
- Prefix booleans with `b`: `bEnableValidation`, `bIsInitialized`.
- Do not encode ownership, scope, or pointer/reference status in a name.
- Declare one variable per line.
- Give variables the narrowest practical scope.
- Prefer initialization at declaration.
- Use an `Out` prefix for output reference parameters. For boolean output
  parameters, use `bOut`, for example `bOutWasCreated`.
- An `In` prefix may disambiguate a template parameter from a nested alias.

```cpp
bool bEnableValidation = true;
constexpr uint32_t MaxFrameCount = 3;

template <typename InElementType>
class TArdaContainer
{
public:
    using ElementType = InElementType;
};
```

## Functions

- Use PascalCase.
- Use a strong verb for commands: `CreateDevice()`, `ResetPipeline()`.
- Boolean-returning functions must ask a clear question: `IsReady()`,
  `HasDevice()`, `ShouldRebuild()`.
- Accessors should describe the returned value: `GetDevice()`,
  `GetBackendType()`.
- Mark methods `const` when they do not modify observable object state.
- Mark functions `noexcept` when failure cannot escape through an exception.
- Apply `[[nodiscard]]` when ignoring a result is probably an error.

## Classes and Structs

- Put the public interface first, followed by protected members, then private
  implementation details.
- Use `class` for types with invariants or behavior and `struct` for passive
  data aggregates.
- Follow the rule of zero. Declare special member functions only when ownership
  or lifetime behavior requires it.
- Mark single-argument constructors `explicit`.
- Use `override` for overrides and `final` when further inheritance is not
  intended.
- Prefer composition over inheritance.

## Formatting

- Use four spaces for indentation; do not use tabs.
- Put opening braces on a new line.
- Always brace control-flow bodies, including one-line bodies.
- Keep one statement per line.
- Keep pointer and reference symbols next to the type:
  `ArdaDevice* Device`, `const ArdaConfig& Configuration`.
- Keep lines readable; split long expressions by logical structure.
- Let the repository formatter decide mechanical details where configured.

## Headers and Includes

- Use `#pragma once`.
- A `.cpp` file includes its matching header first, except where the build
  requires a precompiled header first.
- Then include project headers, third-party headers, and standard-library
  headers in separate groups.
- Include what the file uses; do not depend on transitive includes.
- Prefer forward declarations when they reduce coupling without obscuring code.
- Keep public headers minimal and free of private implementation dependencies.

## Constness, Ownership, and Portability

- Prefer const-correct APIs and locals.
- Pass non-trivial read-only values by `const` reference.
- Use references for required non-null objects and pointers for optional
  objects.
- Express ownership with RAII and smart pointers; avoid owning raw pointers.
- Use fixed-width integer types when width is part of a file format, protocol,
  GPU interface, or ABI.
- Never place `const` on a value return type.

## Macros

Avoid macros when a constant, function, template, or language feature works.
Project macros use uppercase snake case and begin with `ARDA_`.

```cpp
#define ARDA_ENABLE_GPU_VALIDATION 1
```

## Comments

- Prefer self-documenting code.
- Explain intent, constraints, and non-obvious tradeoffs, not syntax.
- Keep comments synchronized with behavior.
- Every function and variable declared in a `Public` header must have a concise
  Doxygen documentation comment using the `/** ... */` style.
- Clearly state what the documented function or variable does and what it is
  for. Include parameters, return values, ownership, lifetime, or constraints
  when they are relevant to correct use.
- Use respectful, precise, and professional language.

## Applying the Convention

New project code must follow this document. When modifying a non-conforming
project-owned type or source file, rename it and update its references when the
change is reasonably contained. Do not rename third-party or generated code.

## Reference

- [Epic C++ Coding Standard for Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)
