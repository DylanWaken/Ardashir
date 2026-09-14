# ArdaRenderGraph Coding Conventions

These conventions apply to project-owned C++ types in `Source/ArdaInfra/ArdaRenderGraph` and supplement the [repository conventions](../ArdaCodingConventions.md).

## Public names

Use the project-wide `Arda` stem with the applicable category prefix: `FArda` for classes and structs, `IArda` for interfaces, `EArda` for enums and `TArda` for templates. Examples include `FArdaDependencyGraph`, `FArdaInductor`, `EArdaDependencyAccess` and `TArdaDependencyNodeDefinition<Parameters>`. Public APIs describe persistent graph concepts; native recording implementation remains private.

## Node authoring

Give every node class its own parameter struct, including nodes with identical fields or no inputs. Do not reuse another node's parameter type, alias it, or pass a shared catch-all struct to multiple node classes. Include only that operation's inputs and outputs and use named fields instead of positional resource arrays. Shared immutable data may be referenced explicitly, while deliberately dynamic pointees need a documented synchronization contract. Each attached node instance retains a separate immutable parameter snapshot; edits to a caller's struct do not update attached nodes.

Initialize nodes with either a prefilled `Node::FArdaParameters` value, an inline aggregate, or `Graph.AttachOrFind<Node>(Name, [](Node::FArdaParameters& Parameters) { ... })`. The callback fills a fresh value-initialized struct synchronously inside the graph edit and may return `FArdaRHIStatus` to reject initialization. It must not retain the temporary reference or edit the graph. Both forms use the same validation, resource declaration, deduplication and preparation path; changed semantics require removing and reattaching the node.

Derive class-authored operations from `TArdaDependencyNode<Derived, Parameters, Kind>` or its graphics, compute, copy, CUDA or synchronization specialization in `ArdaDependencyNode.h`. Implement static `GetMetadata`, `GetCanonicalKey`, `Describe` and the domain's execution hook: `Record` for native work or `PrepareCuda` for CUDA sequences. Synchronization nodes may omit execution. The base enforces this contract at compile time and owns registration, attachment, parameter retention and preparation lifetime. Do not duplicate or hide its attachment/registration methods in derived nodes.

Keep shader registration, binding metadata, layout creation and fixed pipeline configuration inside each node's implementation. Declare an opaque nested `FArdaState` and implement `Prepare(Device)` for immutable setup shared by live instances on that device. Declare `FArdaInstanceState` and implement `CreateInstance(Device, Parameters, State)` for private mutable bindings or parameter-dependent setup. Defaults handle nodes without setup. Validate inputs through `Validate`; return failures and never submit GPU work from attachment hooks. Device state is weakly cached by the base and strongly retained only by live bound instances. Shared state must be immutable; different nodes may record concurrently.

Encode semantic values and full resource identities in canonical keys without structure padding. Derive accesses, pipelines, cost and workspace from the same frozen parameters. Keep semantic pointees immutable; document deliberately dynamic inputs and their synchronization contract. The lower-level typed/erased definition APIs remain available for runtime adapters; their public attachment schema and optional prepared execution schema must be distinguished explicitly.

Declare every resource access with its direction and precise byte or texture-subresource range. A region with one producing node keeps attachment-independent producer-to-reader ordering. When two or more distinct nodes write the same region, attach its operations in their intended execution order: overlapping writes and intervening reads follow original successful attachment order. A read observes the preceding write; an owned read before the first write fails, while an import may supply initial contents. Disjoint regions remain independent. Repeated writes reuse the same logical handle and physical graph resource; declare separate outputs when older values must remain available.

Finding an existing node does not move its attachment order. Removing and attaching a replacement appends a new operation; cancellation restores the earlier graph and order. Use explicit edges for effects that resources do not describe. They may add constraints but cannot contradict resource hazards. Record through `FArdaDependencyExecutionContext`, resolve only declared resources and propagate RHI failures. Supply stages and settings for automatic pipeline inference instead of hiding manual native PSO creation inside ordinary graph operations.

## Edits and execution

Restrict mutations to explicit edit transactions. Reuse compiled graphs across frames and edit when semantics change. Treat completion tickets and retained input/output lifetime as part of the operation contract. Keep compiler and native execution details private.
