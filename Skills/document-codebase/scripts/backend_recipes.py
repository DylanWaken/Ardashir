#!/usr/bin/env python3
"""Publish source-backed resource/capability recipes; fail when coverage or excerpts drift."""
from __future__ import annotations

import argparse
import collections
import html
import json
import re
import textwrap
from pathlib import Path

from sync_api_inventories import evaluate_api

ROOT = Path(__file__).resolve().parents[3]
DOCS = ROOT / "Docs/ArdaBackend"
EXTENDED = "Source/ArdaBackend/Tests/ArdaExtendedRHIParityTests.cpp"
BACKEND = "Source/ArdaBackend/Tests/ArdaBackendTests.cpp"
LIFETIME = "NativeTransientResourcesAndDescriptorsReturnToBaseline"

# Authored explanations and links select examples; executable code and capability
# expressions are read from the compiled tests, never maintained as duplicate snippets.
RESOURCES = [
    ("Device", "VerifyExtendedCommands", "GetDevice()", "Factory and submission authority", "Obtain the device after ConfigureBackend and InitializeBackend succeed. It owns capability reporting, object creation and queue submission. Every resource used together must have this device identity."),
    ("Texture", LIFETIME, "CreateTexture(", "Allocate shaped image storage", "Choose dimensions, format, mip count and all required usage bits before allocation. A render target that will later be sampled needs both usages. Views select subresources; they do not add missing allocation permissions."),
    ("TextureReference", LIFETIME, "CreateTextureReference(", "Keep a stable texture identity", "A texture reference is a retargetable reference to a texture. Use SetTextureReference when replacing streamed storage, then rebuild dependent bindings as needed; changing a reference does not rewrite an already recorded command list."),
    ("Buffer", LIFETIME, "CreateBuffer(", "Store linear and structured data", "mByteSize allocates bytes; mStructureStride describes structured elements. Set the shader, vertex, index, indirect or storage usages actually required. Upload through an open command list and order consumers after the copy."),
    ("UniformBuffer", LIFETIME, "CreateUniformBuffer(", "Upload shader constants", "A uniform buffer carries constants rather than arbitrary UAV storage. Its byte layout must match the shader parameter layout. Use the uniform-buffer write path and preserve the version consumed by each in-flight submission."),
    ("Heap", "VerifyExplicitHeapAliasing", "CreateHeap(", "Place resources in explicit memory", "Query each virtual resource's memory requirements, allocate a compatible heap, and bind at an aligned offset. Reusing an offset requires non-overlapping GPU lifetimes and an aliasing barrier; retaining two resource handles does not make overlapping use valid."),
    ("StagingTexture", "VerifyExtendedCommands", "CreateStagingTexture(", "Move image data between CPU and GPU", "Staging storage has CPU access and native row/depth pitch. Copy to it, complete the GPU transfer, map it and walk rows using mRowPitch rather than width times pixel size. Unmap before reuse."),
    ("EventQuery", "VerifyQueryExecution", "SignalEventQuery(", "Observe a queue completion point", "Create, signal on the producer queue, then poll or wait. An unsignaled query cannot be waited on and a signaled query must be reset before reuse. Event queries report completion, not elapsed time."),
    ("TimerQuery", "VerifyQueryExecution", "BeginTimerQuery(", "Measure GPU elapsed time", "Record BeginTimerQuery and EndTimerQuery around the workload on one list. After submission completes, GetTimerQuerySeconds returns GPU elapsed seconds. Reading before completion or before recording is a state error."),
    ("GpuFence", "VerifyQueryExecution", "SignalGpuFence(", "Retire CPU-owned frame data", "Signal the fence after producer work, then poll or wait before reusing its CPU-side allocation slot. Reset after completion. For GPU-to-GPU dependencies use QueueWait with a submission value instead of a CPU fence wait."),
    ("ShaderResourceView", LIFETIME, "CreateShaderResourceView(", "Read a selected resource interpretation", "An SRV retains its backing resource and selects a compatible range, mip or format. The backing allocation must include shader-resource usage. Transition it to ShaderResource before recording the consumer."),
    ("UnorderedAccessView", LIFETIME, "CreateUnorderedAccessView(", "Expose writable shader storage", "A UAV retains a buffer or texture with unordered-access usage. Its range and format must fit the allocation. Consecutive writes or read-after-write operations still require ordering; a view does not synchronize the resource."),
    ("Sampler", LIFETIME, "CreateSampler(", "Select texture filtering and addressing", "Samplers contain filtering, addressing, comparison and reduction policy, not image storage. Bind them in a sampler slot alongside the texture SRV. Equivalent descriptions may reuse the device's bounded sampler cache."),
    ("Shader", "CreateExtendedShader", "FArdaRHIShaderDesc Desc", "Create a shader from backend bytecode", "The helper loads the artifact selected by the exact backend name and supplies stage and entry point. DXIL and SPIR-V are not interchangeable. Bytecode may be released after successful creation; the shader handle is retained by dependent pipelines."),
    ("ShaderLibrary", "VerifyShaderLibraryExecution", "CreateShaderLibrary(", "Share a compiled shader library", "Create the library from real backend bytecode, then obtain a shader by entry point and stage. A library alone is not executable. The complete example creates a compute pipeline, dispatches it and checks its output."),
    ("InputLayout", LIFETIME, "FArdaRHIVertexAttributeDesc Attribute", "Describe vertex memory", "Describe each attribute's format, semantic, offset, buffer slot and element stride. The vertex buffer contents and vertex shader inputs must agree. Input layout identity is derived from attributes rather than the identity of a shader object."),
    ("BindingLayout", LIFETIME, "FArdaRHIBindingLayoutDesc BindingLayoutDesc", "Declare shader-visible slots", "A layout declares slot type, count, register space and stage visibility. Create the layout before the binding set and use it in the pipeline description. It specifies an interface; it does not allocate the resources to populate that interface."),
    ("BindingSet", LIFETIME, "FArdaRHIBindingSetDesc BindingSetDesc", "Populate a declared layout", "Each binding item supplies a retained resource and optional view for a declared slot. Types and array indices must match the layout. Set the binding set on the graphics/compute/ray state used by the command list."),
    ("DescriptorTable", "VerifyBindlessDescriptorTable", "CreateDescriptorTable(", "Index an extensible descriptor collection", "Create a bindless layout with its capacity and register-space policy, allocate a table, resize if required and write indexed descriptors. Check the precise indexing capabilities required by the shader; bindless support alone does not imply direct heap indexing."),
    ("ResourceCollection", "VerifyExpandedDescriptorsAndResourceCollections", "CreateResourceCollection(", "Maintain an indexed set of resources", "A collection retains typed items and exposes a descriptor table to the shader. UpdateResourceCollection replaces one indexed item; descriptor versioning preserves already recorded uses. The example dispatches before and after replacement and verifies both outputs."),
    ("Framebuffer", LIFETIME, "FArdaRHIFramebufferDesc FramebufferDesc", "Select raster attachments", "Attach texture subresources with render-target/depth usage. Their dimensions, samples and formats must match the graphics or mesh pipeline. The framebuffer retains its attachments; transition them before drawing."),
    ("GraphicsPipeline", LIFETIME, "FArdaRHIGraphicsPipelineDesc GraphicsDesc", "Combine raster shaders and fixed state", "Supply vertex/pixel shaders, vertex layout, attachment formats and fixed raster/depth/blend state. Bind the result through FArdaRHIGraphicsState with a compatible framebuffer. Immutable descriptions allow pipeline reuse."),
    ("ComputePipeline", LIFETIME, "FArdaRHIComputePipelineDesc ComputeDesc", "Bind a compute shader interface", "Supply a compute shader and its ordered binding layouts, then record SetComputeState followed by Dispatch. Grid dimensions count thread groups; the HLSL numthreads declaration determines threads per group. This path also implements CUDA operand alternatives."),
    ("MeshletPipeline", "VerifyMeshPipelineCapabilityAndExecution", "CreateMeshletPipeline(", "Generate raster geometry on the GPU", "Check mMeshShaderTier before creating a mesh/amplification pipeline. Match render-target formats, bind FArdaRHIMeshletState and dispatch mesh work. The complete example reads back rendered pixels rather than treating pipeline creation as proof of execution."),
    ("AccelStruct", "VerifyAccelerationStructureLifecycleAndStateParity", "CreateAccelStruct(", "Build ray-tracing geometry and instances", "BLAS geometry and TLAS instances require distinct descriptions and build ordering. Query build/storage sizes and preserve referenced geometry/BLAS lifetime through traversal. Update and compaction are separately gated. Graphics acceleration structures cannot be reinterpreted as CUDA/OptiX traversal objects."),
    ("RayTracingPipeline", "VerifyRayTracingPipelineCapabilityAndExecution", "CreateRayTracingPipeline(", "Link ray stages and hit groups", "Declare ray-generation, miss, hit-group and callable exports with payload/attribute sizes and recursion limits. Check the reported ray capabilities and limits. A matching shader table and binding state are required before DispatchRays."),
    ("ShaderTable", "VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect", "CreateShaderTable(", "Connect rays to shader records", "The table retains a ray-tracing pipeline and records its named exports and optional local arguments. Populate records and commit before dispatch. The scene example demonstrates distinct hit groups, local bindings and indirect dispatch, with output assertions."),
    ("WorkGraphPipeline", "VerifyD3D12WorkGraphExecution", "CreateWorkGraphPipeline(", "Launch a native work graph", "QueryWorkGraphSupport and mWorkGraphTier gate this path. The checked-in implementation is D3D12-specific; Vulkan reports Unsupported. The library defines node records, and the caller supplies entry records and backing-memory requirements."),
    ("ShaderBundle", "VerifyShaderBundleExecution", "CreateShaderBundle(", "Batch shader dispatch records", "A bundle stores records for supported compute/mesh dispatch families. Record data and binding conventions must match the bundle's pipeline. It does not imply native work-graph support; query mbShaderBundleDispatch independently."),
    ("SamplerFeedbackTexture", "VerifySamplerFeedbackStateParity", "CreateSamplerFeedbackTexture(", "Record texture streaming demand", "Pair the feedback object with the sampled texture and an explicit mip-region layout. Clear, write feedback in the shader and decode before reading demand. This specialized object is capability-gated and has no CUDA surface representation."),
    ("OpacityMicromap", "VerifyVulkanOpacityMicromapLifecycleAndStateParity", "CreateOpacityMicromap(", "Attach per-microtriangle opacity", "Build micromap data with matching triangle subdivision/format usage counts, then reference it in compatible BLAS geometry. The checked-in native path is Vulkan-only and separately capability-gated. Preserve both the micromap and its build inputs through GPU completion."),
    ("RasterState", LIFETIME, "CreateRasterState(", "Reuse rasterization policy", "Create a cached immutable raster-state value describing fill, cull and related raster options. GetDesc supplies the value used by FArdaRHIGraphicsPipelineDesc::mRasterState. Unsupported raster features must be rejected by capability checks before pipeline creation."),
    ("BlendState", LIFETIME, "CreateBlendState(", "Describe attachment blending", "The cached blend-state object stores per-target factors, operations and write masks. Copy GetDesc into a compatible pipeline's blend state. This object has no standalone command execution and no CUDA representation."),
    ("DepthStencilState", LIFETIME, "CreateDepthStencilState(", "Control depth and stencil tests", "Choose depth read/write and comparison policy and compatible stencil operations. Copy GetDesc into the pipeline and supply an appropriate depth/stencil attachment. Disabling depth in the example permits a color-only framebuffer."),
    ("CommandList", "VerifyQueryExecution", "CreateCommandList(", "Record and submit ordered work", "Create the list for an available queue, Open it, record work, Close it and submit through the device. Recording is caller-serialized. Reset discards a previous recording; CUDA CiG captures additionally enforce single submission and capture order."),
]

# Probe names come from the existing executable capability matrix. Every advertised
# fact gets its exact guard and the same workload the conformance suite dispatches.
PROBES = {
    "Contract": ("Unsupported raster policy", "VerifyCapabilityInvariants", "These portable flags currently remain false: no executable variable-rate or conservative-raster path is advertised. Query the flag and select an ordinary raster pipeline. A true flag without a workload is deliberately a conformance failure."),
    "PipelineCache": ("Persist native pipeline compilation", "VerifyShaderLibraryExecution", "Use a writable per-backend cache directory. The conformance workload executes a shader, checks that a cache file exists and repeats startup from it. Persistence accelerates compatible pipeline creation; it does not replace deployable shader artifacts."),
    "ExtendedCommands": ("Copy, stage, transition and dispatch indirectly", "VerifyExtendedCommands", "Copies preserve image subresource identity and native pitch. Explicit/split transitions require paired begin/end semantics, and indirect buffers must contain the API's argument layout. The workload uploads known bytes, records the operations and validates state and readback."),
    "Resolve": ("Resolve multisampling and select planes", "VerifyResolveAndPlaneTracking", "Resolve requires compatible formats and sample counts; copying bytes is not an MSAA resolve. Plane selection matters for depth/stencil views. The example validates both resolved pixels and the independently tracked native/facade states."),
    "HeapAliasing": ("Reuse explicit GPU memory", "VerifyExplicitHeapAliasing", "Virtual resources are created before their memory is bound. Size and alignment come from native memory requirements, not guessed format sizes. An alias barrier changes which overlapping resource is valid; queue/fence ordering must ensure the previous user's lifetime has ended."),
    "Bindless": ("Address descriptors by array index", "VerifyBindlessDescriptorTable", "Bindless tables describe shader-visible indices rather than arbitrary native handles. Capacity, register spaces and descriptor types must agree with the shader. The example writes a table, dispatches through it and verifies the selected resource's data."),
    "RuntimeDescriptors": ("Update descriptors without rewriting recorded work", "VerifyRuntimeDescriptorVersions", "Runtime arrays, partially bound arrays, variable counts and update-after-bind are separate capabilities. The implementation preserves recorded descriptor versions; do not interpret these flags as permission to overwrite data still used by a GPU submission."),
    "DescriptorBuffer": ("Use native descriptor-buffer storage", "VerifyBindlessDescriptorTable", "The Vulkan descriptor-buffer path is distinct from a conventional descriptor set and from direct resource/sampler heap indexing. The conformance dispatch also runs the combined resource/sampler example with descriptor-buffer selection enabled."),
    "DirectResourceHeap": ("Index the resource descriptor heap", "VerifyBindlessDescriptorTable", "Set direct-heap indexing in the bindless layout and compile a shader for that binding model. The queried flag covers resource descriptors only; sampler heap access has its own capability and layout."),
    "DirectResourceAndSamplerHeaps": ("Index resource and sampler heaps together", "VerifyDirectResourceAndSamplerHeapIndexing", "Both direct-indexing flags must hold for a shader using both heaps. Keep sampler and resource index spaces distinct. The example binds both tables and checks texture sampling through the selected indices."),
    "ExpandedDescriptors": ("Use resource collections", "VerifyExpandedDescriptorsAndResourceCollections", "Collections preserve stable indices while resources change. The test compares shader output before and after an indexed update and checks retention. This feature does not automatically make a collection itself CUDA-addressable."),
    "Subgroup": ("Use subgroup arithmetic", "VerifyComputeArithmetic", "Use the subgroup capability and size bounds when selecting a shader variant; a subgroup size is not necessarily the number of threads in a group. The ArdaSubgroup/SubgroupCS shader is executed with a CPU-checked result."),
    "Float16": ("Select native half-precision arithmetic", "VerifyComputeArithmetic", "The native float16 flag describes shader arithmetic, not only half-format image storage. The ArdaFloat16/Float16CS workload checks executable arithmetic. Use a full-precision shader alternative when the flag is false."),
    "Int8": ("Select packed integer dot products", "VerifyComputeArithmetic", "The native int8 flag covers packed signed/unsigned 8-bit dot products with a 32-bit accumulator. It does not advertise arbitrary 8-bit scalar instructions. The ArdaInt8/Int8CS workload checks the packed result against a CPU oracle."),
    "QueueBreadth": ("Order work across queue families", "VerifyQueueBreadth", "Queue availability, dedicated families and GPU waits are independent facts. Submit the producer first and pass its returned submission value to QueueWait before recording dependent execution. Vulkan queue-family transitions are required when ownership actually changes."),
    "SparseResidency": ("Map reserved buffers and textures", "VerifySparseResidencyAndStreamingBudget", "Reserved resources separate virtual address range from physical tile mappings. Query tile shape, packed mips and the sparse-binding queue before mapping. Unmapped regions and alias reuse need explicit lifetime policy; a budget query alone does not allocate or evict data."),
    "StreamingBudget": ("Observe and reserve a streaming budget", "VerifyStreamingBudgetExecution", "Read local/nonlocal usage and budget, then request a reservation only when supported. Budget telemetry is a planning input, not a guarantee that every later allocation will succeed. Keep headroom and handle allocation failure."),
    "ShaderBundle": ("Dispatch a shader bundle", "VerifyShaderBundleExecution", "A bundle batches explicitly declared dispatch records. It is useful without native work graphs and uses its own support query. The sample checks execution and output rather than only successful creation."),
    "WorkGraph": ("Dispatch a D3D12 work graph", "VerifyD3D12WorkGraphExecution", "The checked-in native work-graph implementation is D3D12 compute-node execution. Vulkan and unsupported D3D12 tiers return Unsupported. Entry-record layout comes from the compiled node library; a shader bundle can provide an application-chosen alternative."),
    "MeshShader": ("Rasterize mesh-shader output", "VerifyMeshPipelineCapabilityAndExecution", "Admit a meshlet pipeline only when the mesh tier qualifies. Attachment formats and sample count still obey ordinary raster compatibility. The workload verifies actual pixels, including the optional mesh-bundle path."),
    "RayTracingPipeline": ("Dispatch ray-tracing stages", "VerifyRayTracingPipelineCapabilityAndExecution", "Infrastructure, hardware traversal and pipeline shaders are separate facts. Respect identifier/record/table alignments and recursion/dispatch limits. A zero payload limit means the backend exposes no queryable bound, not that zero-byte payloads are the only valid choice."),
    "AccelerationStructure": ("Build, update and compact acceleration structures", "VerifyAccelerationStructureLifecycleAndStateParity", "Select BLAS or TLAS support explicitly, then inspect update and compaction flags before choosing those operations. The example queries requirements, builds, validates state and exercises allowed lifecycle operations. Graphics AS storage is not a CUDA traversal handle."),
    "InlineRayQuery": ("Trace rays from an ordinary compute shader", "VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect", "Inline ray queries traverse a built TLAS without dispatching a ray-generation pipeline. Select the inline shader branch in the complete scene example. It still requires hardware traversal, valid instances and ordered AS construction."),
    "InstanceBuffer": ("Build a TLAS from instance data", "VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect", "The instance-buffer branch creates native-compatible instance records before building the TLAS. Distinguish GPU-provided instance data from indirect ray dispatch; they have different argument layouts and capability gates."),
    "RayTracingScene": ("Use hit groups, local arguments and indirect rays", "VerifyRayTracingSceneHitGroupsLocalArgumentsAndIndirect", "The scene assigns distinct records to hit groups, supplies local arguments and verifies indirect dispatch output. Persistent shader tables and local descriptor arguments require explicit record/commit semantics and GPU lifetime retention."),
    "OpacityMicromap": ("Build Vulkan opacity micromaps", "VerifyVulkanOpacityMicromapLifecycleAndStateParity", "Micromap support is independent of ordinary ray tracing and is currently implemented by Vulkan only. Formats, usage counts and subdivision levels must agree with the geometry. Build ordering and compaction queries are checked by the example."),
    "SamplerFeedback": ("Decode native sampler feedback", "VerifySamplerFeedbackStateParity", "The D3D12 path pairs a feedback map with a texture and supports the qualified tier's addressing/view restrictions. Run both minimum-mip and mip-region-used variants. Vulkan currently reports this feature unavailable."),
    "CustomPresent": ("Integrate a custom presentation callback", "VerifyD3D12CustomPresentExecution", "The callback can consume the acquired back buffer and choose whether native Present is needed. Its callback storage and image references must outlive use. Both native APIs have Windows examples; resize and repeated present are validated."),
    "Queries": ("Measure and observe submitted work", "VerifyQueryExecution", "Event/fence objects track completion; timer queries measure elapsed GPU time. Polling an unsignaled/reset object differs from waiting for submitted work. Reset only after the corresponding completion and keep queue provenance clear."),
    "ShaderLibrary": ("Execute an entry point from a library", "VerifyShaderLibraryExecution", "A library is a retained bytecode container. Resolve an entry point and stage, build a matching pipeline and dispatch. Shader artifacts remain backend-specific even when the public creation API is shared."),
}


def source_block(source: str, name: str) -> str:
    """Extract a whole C++ function/test, ignoring braces inside strings and comments."""
    text = (ROOT / source).read_text(encoding="utf-8-sig")
    pattern = (r"(?m)^TEST\(\w+,\s*" + re.escape(name) + r"\)"
               if name == LIFETIME else r"(?m)^    (?:void |CreateExtendedShader\b)" + (re.escape(name) + r"\s*\(" if name != "CreateExtendedShader" else ""))
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"{source}: missing example function {name}")
    start = match.start()
    if name == "CreateExtendedShader":
        start = text.rfind("\n", 0, start - 1) + 1
    body_line = re.search(r"(?m)^[ \t]*\{\s*$", text[match.end():])
    if not body_line:
        raise ValueError(f"{source}: no body for {name}")
    opening = text.index("{", match.end() + body_line.start())
    # The existing comment/string-aware scanner preserves complete initializer lists.
    from validate_docs import Validator
    body = Validator.balanced(text[start:], text[start:opening], "{")
    end = opening + len(body)
    return textwrap.dedent(text[start:end]).strip()


def excerpt(code: str, needle: str) -> str:
    """Show a complete paragraph around the critical call; the whole example is linked."""
    offset = code.index(needle)
    start = code.rfind("\n\n", 0, offset)
    end = code.find("\n\n", offset)
    return textwrap.dedent(code[start + 2 if start >= 0 else 0:end if end >= 0 else len(code)]).strip()


def capabilities() -> list[dict]:
    text = (ROOT / EXTENDED).read_text(encoding="utf-8")
    block = text.split("static const FCapabilityDefinition Definitions[] = {", 1)[1].split("static const std::vector", 1)[0]
    cases = [{"name": name, "expression": re.sub(r"\s+", " ", expression).strip(), "probe": probe}
             for name, expression, probe in re.findall(r'ARDA_CAPABILITY\("([^"]+)",\s*(.*?),\s*(\w+)\)', block, re.S)]
    if len(cases) != block.count('ARDA_CAPABILITY("'):
        raise ValueError("A capability predicate could not be parsed")
    header = (ROOT / "Source/ArdaBackend/Public/RHI/ArdaRHICapabilities.h").read_text(encoding="utf-8")
    fields = set(re.findall(r"\b(?:bool|uint32_t|uint64_t|EArdaRHI\w+Tier)\s+(m\w+)\s*=", header))
    expressions = "\n".join(c["expression"] for c in cases)
    missing = {field for field in fields if not field.startswith("mbRequire") and not re.search(r"\." + field + r"\b", expressions)}
    if missing:
        raise ValueError(f"Capabilities without a documented executable predicate: {missing}")
    return cases


def page(title: str, intro: str, sections: list[tuple[str, str, str]], diagram: str, checkpoints: str = "", objectives: str = "") -> str:
    template = (DOCS / "index.html").read_text(encoding="utf-8")
    template = re.sub(r"<title>.*?</title>", f"<title>{html.escape(title)} | ArdaBackend</title>", template)
    template = re.sub(r'<meta name="description" content="[^"]*">', f'<meta name="description" content="{html.escape(intro, quote=True)}">', template)
    template = template.replace('aria-current="page" href="index.html"', 'href="index.html"')
    links = "".join(f'<a href="#{identifier}">{html.escape(label)}</a>' for identifier, label, _body in sections)
    article = f'''<article><header class="page-header"><p class="eyebrow">Executable examples and capability admission</p><h1>{title}</h1><p>{intro}</p></header>
<nav class="toc" aria-label="On this page"><h2>On this page</h2><a href="#objectives">How to use these examples</a>{links}<a href="#summary">Checkpoints</a></nav>
<section id="objectives"><h2>How to use these examples</h2>
<p>Learn to select a resource, admit the exact feature it needs and order its GPU use. First read <a href="rhi.html">resource ownership and data layout</a>, <a href="commands.html">command recording and synchronization</a>, and <a href="getting-started.html">backend initialization</a>.</p>
<p>The mental model is allocation → view/binding → recorded use → submission → completion → reuse. A <a class="term" data-term="capability" href="../glossary.html#term-capability">capability</a> admits an operation; it does not establish a resource's state, memory residency or queue ownership. Creation can still fail for invalid descriptors or exhausted memory.</p>
<figure><img src="Assets/{diagram}" alt="{html.escape(title)} ownership and execution flow"><figcaption>Representations and references preserve access and lifetime; commands and fences establish order.</figcaption></figure>
<p>Short snippets are literal excerpts from compiled GoogleTest workloads. They use ASSERT_TRUE/EXPECT_EQ to make failures visible. Adapt those checks to your application's status handling. Variables and helpers are initialized in each linked complete example; snippets are not separate standalone programs. The complete examples show allocation, binding, execution, readback assertions and cleanup.</p>
<pre><code class="language-shell">cmake --build build --config Debug --target ArdaBackendTests
ctest --test-dir build -C Debug -R ArdaBackend --output-on-failure</code></pre>
<p>Use the platform's actual executable directory for direct --gtest_filter runs. Shader artifacts and fixtures are deployed by the test build. Missing validation skips dependent GPU tests; see <a href="validation.html">validation setup and failure diagnosis</a>. A capability skip is not evidence that the skipped operation ran.</p></section>'''
    article += "\n".join(f'<section id="{identifier}"><h2>{html.escape(label)}</h2>{body}</section>' for identifier, label, body in sections)
    article += '''<section id="summary"><h2>Checkpoints and further reading</h2><p>Before submitting work, identify the exact capability, allocation usage, binding layout and producer/consumer states. Keep referenced objects alive through GPU completion. Select an explicit alternative when admission fails; do not reinterpret an opaque handle or suppress an unrelated validation error.</p><ol><li>Why can a supported resource creation still fail?</li><li>Which fence proves that aliased memory can be reused?</li><li>Why does a texture view not imply CUDA surface support?</li><li>How do you distinguish an unavailable feature from a malformed descriptor?</li></ol><p>Continue with <a href="rhi-feature-guide.html">feature mechanisms and native differences</a>, <a href="cuda-interop.html">CUDA operands and graphics alternatives</a>, <a href="diagnostics.html">diagnostics</a>, and the <a href="api-reference.html">canonical API reference</a>.</p></section></article>'''
    if checkpoints:
        article = re.sub(r'<section id="summary">.*?</section>', lambda _: '<section id="summary"><h2>Checkpoints and further reading</h2>' + checkpoints + '</section>', article, flags=re.S)
    if objectives:
        article = re.sub(r'<section id="objectives">.*?</section>', lambda _: '<section id="objectives"><h2>Learning objectives and prerequisites</h2>' + objectives + '</section>', article, flags=re.S)
        article = article.replace('href="#objectives">How to use these examples', 'href="#objectives">Learning objectives')
    if diagram == "diagnostic-escalation.svg":
        article = article.replace("Representations and references preserve access and lifetime; commands and fences establish order.",
            "Start with the initialization result and native diagnostic; escalate to the validation layer and GPU tooling for successfully initialized workloads.")
    # A callable replacement preserves C++ escapes such as \\n in copied string literals.
    return re.sub(r"<article>.*?</article>\s*</main>", lambda _: article + "\n</main>", template, count=1, flags=re.S)


def generate() -> dict[Path, str]:
    api = evaluate_api((ROOT / "Docs/assets/backend-api.js").read_text(encoding="utf-8"), "ArdaBackendApi")
    symbols = api["symbols"]
    def api_link(name: str) -> str:
        match = next((s for s in symbols if s["qualifiedName"] == name), None)
        if not match:
            match = next((s for s in symbols if s["name"] == name or s["qualifiedName"].endswith("::" + name)), None)
        if not match:
            raise ValueError(f"No canonical API link for {name}")
        return f'<a href="api-reference.html#{match["id"]}"><code>{html.escape(name)}</code></a>'
    blocks: dict[str, str] = {}
    def example(name: str) -> str:
        if name not in blocks:
            blocks[name] = source_block(BACKEND if name == LIFETIME else EXTENDED, name)
        return blocks[name]
    resource_sections = []
    resource_coverage = {}
    for kind, function, needle, title, prose in RESOURCES:
        code = excerpt(example(function), needle)
        identifier = "resource-" + kind.lower()
        cuda = ("Buffer/texture CUDA eligibility is per allocation. Opt in at creation and inspect GetCudaResourceInfo; see the CUDA chapter."
                if kind in {"Texture", "Buffer"} else "This object is not a CUDA buffer pointer or surface. Use compatible source buffers/textures when an operand needs shared storage.")
        body = f'<p>{api_link("IArdaRHI" + kind)}: {prose}</p><pre><code class="language-cpp">{html.escape(code)}</code></pre><p>{cuda} Owning RHI references retain the object; submitted lists retain native dependencies until their queue retires.</p><p><a href="gpu-examples.html#{function}">Complete example: {function}</a>. Exact creation signatures and failure contracts are in the API link above.</p>'
        resource_sections.append((identifier, f"{kind}: {title}", body))
        resource_coverage[kind] = f"resource-recipes.html#{identifier}"
    resource_text = (ROOT / "Source/ArdaBackend/Public/RHI/ArdaRHIResource.h").read_text(encoding="utf-8")
    declared = set(re.findall(r"\b(\w+)\s*,?", resource_text.split("enum class EArdaRHIResourceType", 1)[1].split("{", 1)[1].split("}", 1)[0])) - {"Count"}
    if declared != set(resource_coverage):
        raise ValueError(f"Resource recipe coverage mismatch: missing={declared-set(resource_coverage)}, stale={set(resource_coverage)-declared}")
    cases = capabilities()
    if not cases:
        raise ValueError("No capability cases found")
    feature_sections = []
    feature_coverage = {}
    groups = collections.defaultdict(list)
    for case in cases:
        groups[case["probe"]].append(case)
    for probe, group in groups.items():
        title, function, prose = PROBES[probe]
        code = example(function)
        guards = "\n".join(f'const bool {c["name"]} = {c["expression"]};' for c in group)
        body = f'<p>{prose}</p><p>Read {api_link("FArdaRHICapabilities")} from {api_link("IArdaRHIDevice")} and select only the abilities used by your workload. These are the exact predicates used by the executable conformance matrix:</p><pre><code class="language-cpp">{html.escape("const auto& C = Device->GetCapabilities();\n" + guards)}</code></pre>'
        if probe == "Contract":
            body += '<pre><code class="language-cpp">if (!C.mbConservativeRasterization || !C.mbVariableRateShading) {\n    // Use the ordinary raster pipeline; neither feature is currently advertised.\n}</code></pre>'
        else:
            # Recording/creation paragraph is accompanied by a complete executable workload.
            calls = re.findall(r"(?:Device|Commands)(?:\.mValue)?->(Create\w+|Dispatch\w*|QueueWait)\(", code)
            focus = calls[0] + "(" if calls else "GetCapabilities()"
            snippet = excerpt(code, focus) if focus in code else code
            body += f'<pre><code class="language-cpp">{html.escape(snippet)}</code></pre>'
        body += f'<p><a href="gpu-examples.html#{function}">Complete usage and output checks: {function}</a>. For each named predicate, run <code>ArdaBackendTests --gtest_filter=*AdvertisedCapabilityConforms/*_NAME</code>, replacing NAME with the predicate name above. The test chooses the corresponding native branch and workload parameters.</p>'
        identifier = "feature-" + probe.lower()
        feature_sections.append((identifier, title, body))
        for case in group:
            feature_coverage[case["name"]] = {**case, "page": f"capability-recipes.html#{identifier}", "example": f"gpu-examples.html#{function}"}
    # Keep the complete function once, even when several resources/capabilities use it.
    examples = [(name, name, f'<p>Source: <code>{BACKEND if name == LIFETIME else EXTENDED}</code>. This is the compiled test helper, including its preconditions and output checks. See the <a href="capability-recipes.html">capability recipes</a> for workload selection.</p><details><summary>Read the complete GPU example</summary><pre><code class="language-cpp">{html.escape(code)}</code></pre></details>') for name, code in blocks.items()]
    extended = (ROOT / EXTENDED).read_text(encoding="utf-8")
    scaffold = extended[:extended.index("    void VerifySamplerFeedbackStateParity")].rstrip() + "\n} // anonymous namespace"
    shader_examples = "".join(
        f'<details id="shader-{path.stem.lower()}"><summary>{path.name}</summary><pre><code class="language-hlsl">{html.escape(path.read_text(encoding="utf-8"))}</code></pre></details>'
        for path in sorted((ROOT / "Source/ArdaBackend/Tests").glob("*.hlsl")))
    examples.insert(0, ("fixture", "Shared fixture and application replacements",
        '<p>The workloads below are functions in the same anonymous namespace as this source-backed scaffold. It defines FExtendedDiagnosticCallback, FExtendedBackendCleanup, LoadExtendedShaderArtifact, CreateExtendedShader and the Expect*State checks. In an application, replace GoogleTest assertions with checked status handling; use a diagnostic sink that lives through ShutdownBackend, an RAII shutdown guard, and a bytecode loader rooted in your deployed shader directory.</p>'
        '<p>ARDASHIR_BUILD_TESTS deploys artifacts and defines ARDA_BACKEND_TEST_SHADER_DIR. The shared ArdaTestBackend.h macro returns a GoogleTest skip only for ValidationUnavailable; see <a href="validation.html#results">validation result handling</a>. Other assertions must remain failures. The lifetime-only example uses CreateArtifactShader for the same artifact-loading operation, FCollectingDiagnosticCallback for error counting, and FExternalTestCleanup for shutdown/provider cleanup; use the scaffold equivalents when adapting it without external providers. Windows presentation examples additionally use the native surface class below.</p>'
        f'<details><summary>Headers, diagnostic sink, shutdown guard, loader and state assertions</summary><pre><code class="language-cpp">{html.escape(scaffold)}</code></pre></details>'
        '<p>Call a workload with the exact linked backend name, native-d3d12 or native-vulkan. Optional booleans and artifact arguments are selected by the corresponding conformance case; use the recipe’s test filter to execute those parameters without assembling a second test runner.</p>'))
    examples.insert(1, ("shader-sources", "Shader interfaces paired with the workloads",
        '<p>The shader and host binding layout form one interface. ResourceCollectionCS reads byte zero of CollectionInputs[Thread.x] from t0 and writes to u0 in space1; the host dispatches two threads and checks the indexed replacement. The ray-scene source declares payloads, hit/miss exports and the backend-specific local-record ABI, so the shader-table byte values can be traced into the checked pixel results. All HLSL below is copied from the files compiled by the test build.</p>' + shader_examples))
    shader_setup = (ROOT / "Source/ArdaBackend/Tests/ArdaBackendTestMain.cpp").read_text(encoding="utf-8")
    examples.insert(2, ("shader-artifacts", "Build the exact shader variants",
        '<p>ArdaLocalRayTracingTest is compiled from ArdaRayTracingTest.hlsl with ARDA_LOCAL_RECORDS defined by EnableLocalRecords. This selects the native local-record ABI; the ordinary ArdaRayTracingTest variant uses fallback constants. Preserve that distinction when testing local argument bytes: omitting the define can produce the expected constants without reading the host records.</p>'
        '<p>The source-backed test environment below pairs each artifact name, source, entry point and compilation hook, then calls EnsureRegisteredShaderArtifacts for each linked backend. This also supplies the resource-collection and graphics-fallback variants used above. CMake supplies the source and output directory definitions; application deployments should use their own writable compiler cache and deployable artifact directory. See <a href="shaders.html">shader registration and compilation</a> for the application workflow.</p>'
        f'<details><summary>Artifact registrations and compilation environment</summary><pre><code class="language-cpp">{html.escape(shader_setup)}</code></pre></details>'))
    outputs = {
        DOCS / "resource-recipes.html": page("Resource recipes", "Choose and use every public RHI resource kind, with allocation, ownership and CUDA representation guidance.", resource_sections, "rhi-data-and-queues.svg"),
        DOCS / "capability-recipes.html": page("Capability recipes", "Admit every capability in the native conformance matrix and follow its executable usage example.", feature_sections, "rt-ml-capability-admission.svg"),
        DOCS / "gpu-examples.html": page("Complete GPU examples", "Read the source-backed workloads behind the resource and capability recipes. Code is regenerated from the compiled tests.", examples, "cpu-gpu-queue-timeline.svg"),
        DOCS / "recipe-coverage.json": json.dumps({"resources": resource_coverage, "capabilities": feature_coverage}, indent=2) + "\n",
    }
    outputs.update(integration_chapters(api_link))
    return outputs


def integration_chapters(api_link) -> dict[Path, str]:
    from cuda_recipes import cuda_chapter
    from cuda_graphics import cuda_graphics_chapter
    def code(value: str, language: str = "cpp") -> str:
        return f'<pre><code class="language-{language}">{html.escape(value)}</code></pre>'
    cuda_page = cuda_chapter(ROOT, api_link, page)
    validation_sections = [
        ("provisioning", "Automatic local provisioning", '<p>With ARDASHIR_BUILD_TESTS enabled, test targets depend on ArdaValidationLayers. D3D12 deploys the matching debug DLL from the pinned Agility SDK NuGet dependency. Vulkan reuses an explicit layer directory, an earlier local install, SDK/environment paths or common Linux paths; otherwise it downloads pinned Khronos sources and builds their pinned dependencies under the build directory.</p><p>The optional Vulkan build needs Git, Python 3.10+, CMake 3.22.1+ and a suitable C++ compiler. It can take several minutes. Download/configure/build/install failures warn and leave the Ardashir build usable. Reconfigure to retry; incremental builds reuse the previous attempt. No system installation or registry modification is performed.</p>'),
        ("options", "Choose a layer installation or offline build", code('cmake -S . -B build -DARDASHIR_PROVISION_VALIDATION=OFF\n# Or point at a loadable manifest/library installation:\ncmake -S . -B build -DARDASHIR_VULKAN_VALIDATION_DIR="C:/Vulkan/layers"\n# Or build a local source checkout (upstream dependencies may need network):\ncmake -S . -B build -DARDASHIR_VULKAN_VALIDATION_SOURCE_DIR="C:/src/Vulkan-ValidationLayers"', 'shell') + '<p>An explicit layer directory suppresses downloads even if it is invalid. An installed layer still works with provisioning OFF. Agility core-runtime deployment remains required for D3D12. Automatic Vulkan source provisioning is skipped while cross-compiling.</p>'),
        ("discovery", "Direct runs and CTest use the same discovery", '<p>Test executables read the generated validation-layers/layer-path.txt before main and prepend it to VK_ADD_LAYER_PATH. Existing additional paths are retained. An explicit VK_LAYER_PATH remains authoritative, including deliberately empty layer directories. Native initialization probes whether the layer can actually load; a JSON manifest alone is insufficient.</p>'),
        ("results", "Handle missing validation without hiding device failures", '<p>' + api_link('EArdaInitializeResult') + ' includes ValidationUnavailable. Headless callers read ' + api_link('GetBackendInitializeResult') + ' after InitializeBackend returns false; presentation startup returns the enum directly. The shared ARDA_REQUIRE_BACKEND test assertion skips only that result and keeps other initialization assertion failures. Presentation smoke programs use the existing CTest skip code 77.</p><p>A failed layer load can emit error diagnostics before returning ValidationUnavailable. Fixture teardown excludes that specific initialization outcome from its zero-GPU-error assertion; successfully initialized workloads still require zero errors. Applications can report the reason or explicitly reconfigure validation policy before retrying.</p>'),
        ("reproduce", "Reproduce missing and broken installations", '<p>Copy a test executable and its D3D12/D3D12Core.dll into an isolated directory, omit d3d12SDKLayers.dll, and set VK_LAYER_PATH to an empty directory. To test broken Vulkan loading, copy only the manifest, leaving its referenced DLL absent. Neither procedure changes the machine’s installed layers.</p><p>Verified runs had zero failures with installed layers, both layers absent, and a stale Vulkan manifest. Backend/RDG tests that require validation skipped; CPU and validation-independent tests continued. All seven presentation smoke tests passed with layers installed, and missing-layer presentation runs returned 77.</p>'),
        ("diagnosis", "Find provisioning and native diagnostics", '<p>Read validation-layers/download.log, checkout.log, configure.log, build.log and install.log for the failed stage. Then inspect GetBackendError for the loader/device result. Missing validation is different from an unavailable graphics loader, unsupported GPU feature, malformed shader, or invalid descriptor. The latter errors are not converted into validation skips.</p><p>Further reading: <a href="https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/e4786f7ce8f1319215eff0d938f4be4651cbb85d/BUILD.md">Khronos build instructions</a>, <a href="https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md">loader discovery</a>, and <a href="https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/">Microsoft Agility deployment</a>.</p>'),
    ]
    return {
        DOCS / "cuda-interop.html": cuda_page,
        DOCS / "cuda-graphics.html": cuda_graphics_chapter(api_link, page),
        DOCS / "validation.html": page("GPU validation setup", "Provision debug layers locally, distinguish native layer failures and keep unavailable validation from failing unrelated tests.", validation_sections, "diagnostic-escalation.svg",
            '<p>Provisioning is best effort; the native loader determines whether validation is usable. Preserve unrelated device and workload failures.</p><ol><li>Which result permits ARDA_REQUIRE_BACKEND to skip?</li><li>Why does an explicit VK_LAYER_PATH override an automatically installed layer?</li><li>Why is a manifest whose library cannot load insufficient?</li><li>Which shader, device and resource errors must remain failures?</li></ol><p>Use <a href="#discovery">discovery</a>, <a href="#results">result handling</a> and <a href="diagnostics.html">diagnostics</a> to check your answers.</p>'),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = generate()
    stale = []
    for path, content in outputs.items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != content:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.write_text(content, encoding="utf-8", newline="\n")
    if stale:
        raise SystemExit("Stale recipe outputs: " + ", ".join(stale))
    print(f"Recipe coverage: {len(RESOURCES)} resource kinds, {len(capabilities())} capability predicates; outputs {'checked' if args.check else 'updated'}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
