# ArdaRenderGraph Coding Conventions

These conventions apply to project-owned C++ types in `Source/ArdaRenderGraph`.
They supplement the repository-wide [Arda Coding Conventions](../ArdaCodingConventions.md)
and take precedence when naming types in this module.

## Type Names

Use the `ARDG` module stem instead of the repository-wide `Arda` project stem.
The applicable Unreal-style category prefix must still come first:

- `FARDG` for ordinary classes, structs, unions, and type aliases.
- `IARDG` for abstract interfaces.
- `EARDG` for enums.
- `TARDG` for class and alias templates.

```cpp
class FARDGRenderGraph;
struct FARDGRenderGraphContext;
class IARDGPassExecutor;
enum class EARDGResourceState;
using FARDGTextureHandle = nvrhi::TextureHandle;

template <typename ElementType>
class TARDGResourcePool;
```

Do not use the general `FArda`, `IArda`, `EArda`, or `TArda` prefixes for
ArdaRenderGraph-owned types.

```cpp
// Correct
struct FARDGRenderGraphContext;

// Incorrect
struct FArdaRenderGraphContext;
struct ARDGRenderGraphContext;
```

This rule applies to classes, structs, enums, unions, aliases, interfaces, and
class or alias templates. It does not change the repository-wide conventions
for filenames, functions, variables, namespaces, or macros.
