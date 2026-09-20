# Registering a graph node

Publish a node as a named callable beside its class declaration. Applications
include the header and write `FillDemoBuffer(Edit, Buffer, uint32_t{7})`; the call
records a fresh operation and registers its implementation on demand. The global
object contains no graph, parameter, device or execution state.

## Declare the class and its invocation name

Give each class its own parameter struct and derive from the matching domain
base. Place `ARDA_DECLARE_GRAPH_NODE(NodeClass, InvocationName);` immediately
after the complete class, in the same namespace. This declares an `extern const`
callable, so every translation unit refers to the same object.

The example fills an existing GPU buffer and exposes it as `Destination`. Save
this header as `ArdaDemoFillNode.h`:

```cpp
#pragma once
#include "Graph/Node/ArdaDependencyNode.h"

namespace arda
{
    struct FArdaDemoFillParameters
    {
        FArdaDependencyResourceHandle mDestination;
        uint32_t mValue = 0;
    };

    class FArdaDemoFillNode final
        : public TArdaComputeDependencyNode<FArdaDemoFillNode, FArdaDemoFillParameters>
    {
    public:
        static FArdaDependencyNodeMetadata GetMetadata();
        static FArdaRHIStatus DeclareResources(
            FArdaDependencyResourceContext& Context, FArdaParameters& Parameters);
        static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
        static FArdaDependencyNodeDesc Describe(
            const FArdaParameters& Parameters, const FArdaState& State);
        static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
            const FArdaParameters& Parameters, const FArdaState& State,
            FArdaInstanceState& Instance);
    };
    ARDA_DECLARE_GRAPH_NODE(FArdaDemoFillNode, FillDemoBuffer);
}
```

The class name and invocation name are independent. Project node types keep the
`FArda` prefix; callable names describe the operation. All project APIs use
`arda`, so choose a callable name unique across that namespace. An exported
callable is a C++ object and cannot share its name with another object or an
overload set. Keep a name such as `CopyTo` for free-function overloads that
dispatch different resource permutations to distinct named node callables.

## Define the callable and implement the contract

Place the matching `ARDA_DEFINE_GRAPH_NODE(NodeClass, InvocationName);` once in
the owning `.cpp`, in the same namespace. Include the header rather than
redeclaring the class. Save this implementation as `ArdaDemoFillNode.cpp`:

```cpp
#include "ArdaDemoFillNode.h"

namespace arda
{
    ARDA_DEFINE_GRAPH_NODE(FArdaDemoFillNode, FillDemoBuffer);

    FArdaDependencyNodeMetadata FArdaDemoFillNode::GetMetadata()
    {
        return {"example.demo.fill-buffer", 1};
    }

    FArdaRHIStatus FArdaDemoFillNode::DeclareResources(
        FArdaDependencyResourceContext& Context, FArdaParameters& Parameters)
    {
        const auto* Resource = Context.Find(Parameters.mDestination);
        if (!Resource || Resource->mbTexture || Resource->mExternalAccelerationStructure ||
            Resource->mBuffer.mCpuAccess != EArdaRHICpuAccess::None ||
            !Resource->mBuffer.mByteSize || Resource->mBuffer.mByteSize % sizeof(uint32_t) ||
            !HasAnyFlags(Resource->mBuffer.mUsage, EArdaRHIBufferUsage::UnorderedAccess))
        {
            return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "FillDemoBuffer requires a GPU UAV buffer with a nonzero multiple-of-four size.");
        }
        return Context.Buffer(Parameters.mDestination, "Destination", Resource->mBuffer);
    }

    eastl::string FArdaDemoFillNode::GetCanonicalKey(const FArdaParameters& Parameters)
    {
        return FArdaDependencyKeyBuilder()
            .Resource(Parameters.mDestination).Value(Parameters.mValue).Build();
    }

    FArdaDependencyNodeDesc FArdaDemoFillNode::Describe(
        const FArdaParameters& Parameters, const FArdaState&)
    {
        FArdaDependencyAccess Access;
        Access.mResource = Parameters.mDestination;
        Access.mAccess = EArdaDependencyAccess::Write;
        Access.mState = EArdaRHIResourceState::UnorderedAccess;
        FArdaDependencyNodeDesc Description;
        Description.mAccesses = {Access};
        return Description;
    }

    FArdaRHIStatus FArdaDemoFillNode::Record(FArdaDependencyExecutionContext& Context,
        const FArdaParameters& Parameters, const FArdaState&, FArdaInstanceState&)
    {
        return Context.GetCommands().ClearBufferUInt(
            *Context.GetBuffer(Parameters.mDestination), Parameters.mValue);
    }
}
```

`GetMetadata` supplies a nonempty process-wide registry name and a positive
implementation version. This runtime identity is separate from the C++ callable
symbol. Keep both stable; change the version when the implementation contract
changes. Registering a conflicting class, schema or version under an existing
name fails with `InvalidArgument`; it does not replace that definition.

`DeclareResources` validates and publishes logical outputs transactionally. An
empty output passed to `Context.Buffer` or `Context.Texture` can instead infer a
transient resource from a descriptor. Return a failure to reject invocation.
`GetCanonicalKey` must encode every behavior-affecting value and resource using
`FArdaDependencyKeyBuilder`, including retained semantic data. Do not hash raw
struct bytes, which may contain padding. The builder's resource encoding also
allows equivalent fresh constructions to reuse persistent graph revisions.

`Describe` declares every access, range, pipeline request and observable side
effect. The default buffer range covers the full buffer in this example. `Record`
resolves only declared resources and propagates native command failures. The
callable itself never retains parameters: each invocation owns its frozen
snapshot independently.

This operation uses the base's empty setup and instance states. Shader nodes can
declare an opaque nested `FArdaState` and implement `Prepare(Device)` for immutable
per-device setup, plus `FArdaInstanceState` / `CreateInstance` for private mutable
state. Keep shader registration and binding metadata in the implementation.
CUDA nodes use `TArdaCudaDependencyNode` and `PrepareCuda` instead of `Record`;
synchronization nodes may omit execution. See the [compiled node recipes](node-recipes.html)
for compute, raster, mesh, work-graph, CUDA and ray-tracing examples, and
[feature requirements](feature-requirements.html) for `GetRequirements` admission.

## Link and invoke the node

Compile `ArdaDemoFillNode.cpp` into the application or node library and link
`Ardashir::ArdaDependencyGraph`. Include `ArdaDemoFillNode.h` where the callable
is used. A missing definition produces an unresolved symbol; defining the same
callable twice violates the one-definition rule and is a link error. The macros
check that the class derives from the node base; registration or invocation
checks the complete hook contract at compile time. Neither macro registers a
node or prepares a device during static initialization.

```cpp
#include "ArdaDependencyGraph.h"
#include "ArdaDemoFillNode.h"

// Device was created by the application.
FArdaDependencyGraph Graph(Device);
FArdaGraphEdit Edit(Graph);
FArdaRHIBufferDesc Desc;
Desc.mByteSize = 16;
Desc.mUsage = EArdaRHIBufferUsage::UnorderedAccess;
auto Buffer = AllocateBuffer(Edit, Desc);

FillDemoBuffer(Edit, Buffer, uint32_t{7});
FArdaDemoFillParameters Parameters{Buffer, 9};
const auto Filled = FillDemoBuffer(Edit, Parameters);
const auto Output = Filled.GetBuffer("Destination");
const auto Bytes = Readback(Edit, Output);

if (auto Status = Edit.Commit(); !Status) { return Status; }
if (auto Status = Graph.Compile(); !Status) { return Status; }
return Graph.Execute().mStatus;
```

The two calls deliberately create two operations; the second write follows the
first. Positional arguments aggregate-initialize fields in declaration order;
omitted fields keep their defaults and narrowing is rejected at compile time.
An initializer callback is also supported. For an explicit diagnostic name,
use the parameter-struct or initializer form and append the name. Diagnostic
names identify operations within one construction and are separate from both
the global callable symbol and registry name.

Calls automatically run the class's registration path. Failures from registration,
feature admission, validation or preparation are retained by the edit; check
`Edit.Commit()` before compiling. Construction errors do not produce a partially
accepted edit. Resolve typed output slots while the edit is open. Execution
failures are reported by `Execute`, `Submit` or ticket completion.

## Register before discovery when needed

Named callable use requires no startup registration. Tools that enumerate nodes
or select by registry string must register their library first:

```cpp
if (auto Status = FArdaDemoFillNode::Register(); !Status) { return Status; }
// Safe to repeat for this same class and implementation version.
const auto Names = FArdaNodeRegistry::Get().GetNames();

FArdaGraphEdit Edit(Graph);
const auto Filled = Edit.Invoke("example.demo.fill-buffer", Parameters);
```

Registry invocation validates the public parameter type; a missing definition
or mismatched schema fails through the edit status. Registration is independent
of any graph or device and performs no GPU work. Library initialization can call
each class's `Register()` and propagate its status. Direct named calls, static
`NodeClass::Invoke` and generic `Edit.Invoke<NodeClass>` all use the same
registration and authoring path. Node authors do not expose raw graph attachment
or implement a separate operation registry.
