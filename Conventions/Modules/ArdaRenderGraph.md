# ArdaRenderGraph Coding Conventions

These conventions apply to project-owned C++ types in `Source/ArdaRenderGraph` and supplement the [repository conventions](../ArdaCodingConventions.md).

## Public names

Use the project-wide `Arda` stem with the applicable category prefix: `FArda` for classes and structs, `IArda` for interfaces, `EArda` for enums and `TArda` for templates. Examples include `FArdaDependencyGraph`, `FArdaInductor`, `EArdaDependencyAccess` and `TArdaDependencyNodeDefinition<Parameters>`. Public APIs describe persistent graph concepts; native recording implementation remains private.

## Node authoring

Derive class-authored operations from `TArdaDependencyNode<Derived, Parameters, Kind>` or its graphics, compute, copy, CUDA or synchronization specialization in `ArdaDependencyNode.h`. Implement static `GetMetadata`, `GetCanonicalKey`, `Describe` and the domain's execution hook: `Record` for native work or `PrepareCuda` for CUDA sequences. Synchronization nodes may omit execution. The base enforces this contract at compile time and owns registration, attachment, parameter retention and preparation lifetime. Do not duplicate or hide its attachment/registration methods in derived nodes.

Keep shader registration, binding metadata, layout creation and fixed pipeline configuration inside each node's implementation. Declare an opaque nested `FState` and implement `Prepare(Device)` for immutable setup shared by live instances on that device. Declare `FInstanceState` and implement `CreateInstance(Device, Parameters, State)` for private mutable bindings or parameter-dependent setup. Defaults handle nodes without setup. Validate inputs through `Validate`; return failures and never submit GPU work from attachment hooks. Device state is weakly cached by the base and strongly retained only by live bound instances. Shared state must be immutable; different nodes may record concurrently.

Encode semantic values and full resource identities in canonical keys without structure padding. Derive accesses, pipelines, cost and workspace from the same frozen parameters. Keep semantic pointees immutable; document deliberately dynamic inputs and their synchronization contract. The lower-level typed/erased definition APIs remain available for runtime adapters; their public attachment schema and optional prepared execution schema must be distinguished explicitly.

Give each logical resource version one producer. Use explicit edges for effects that resources do not describe. Record through `FArdaDependencyExecutionContext`, resolve only declared resources and propagate RHI failures. Supply stages and settings for automatic pipeline inference instead of hiding manual native PSO creation inside ordinary graph operations.

## Edits and execution

Restrict mutations to explicit edit transactions. Reuse compiled graphs across frames and edit when semantics change. Treat completion tickets and retained input/output lifetime as part of the operation contract. Keep compiler and native execution details private.
