"""Generate native feature queries from the selected Agility SDK headers."""

import re
import sys
from pathlib import Path


# These selectors need inputs, or have variable-sized or non-scalar outputs.
# The provider translates enumerated inputs whose meaning can be shared.
SPECIAL_SELECTORS = {
    "ARCHITECTURE",
    "ARCHITECTURE1",
    "BARRIER_LAYOUT",
    "COMMAND_QUEUE_PRIORITY",
    "FEATURE_LEVELS",
    "FENCE_BARRIERS",
    "FORMAT_INFO",
    "FORMAT_SUPPORT",
    "GUID_TEXTURE_LAYOUT",
    "MLIR_COMPUTE_GRAPH_VERSION",
    "MLIR_EXCHANGE",
    "MULTISAMPLE_QUALITY_LEVELS",
    "PLACED_RESOURCE_SUPPORT_INFO",
    "PROTECTED_RESOURCE_SESSION_SUPPORT",
    "PROTECTED_RESOURCE_SESSION_TYPE_COUNT",
    "PROTECTED_RESOURCE_SESSION_TYPES",
    "QUERY_META_COMMAND",
    "ROOT_SIGNATURE",
    "SERIALIZATION",
    "SHADER_CACHE_ABI_SUPPORT",
    "SHADER_MODEL",
}


# Maps SDK members to Arda semantics. A qualified selector disambiguates reused
# field names. Omitted members are covered by the typed RHI facts or lack a
# portable meaning. Tier and mask translations are emitted separately.
FEATURE_NAMES = {
    "DoublePrecisionFloatShaderOps": "ArdaBackendFeature::ShaderFloat64",
    "OutputMergerLogicOp": "rendering.logic-op",
    "PSSpecifiedStencilRefSupported": "shader.fragment-stencil-reference",
    "TypedUAVLoadAdditionalFormats": "shader.extended-typed-storage-image-load",
    "ROVsSupported": "shader.raster-order-views",
    "StandardSwizzle64KBSupported": "texture.standard-64-kib-swizzle",
    "CrossAdapterRowMajorTextureSupported": "resource.cross-adapter-linear-texture",
    "VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation": "shader.viewport-and-layer-routing",
    "MaxGPUVirtualAddressBitsPerResource": "resource.max-address-bits",
    "MaxGPUVirtualAddressBitsPerProcess": "memory.max-process-address-bits",
    "TotalLaneCount": "shader.total-lane-count",
    "ExpandedComputeResourceStates": "synchronization.compute-expanded-resource-states",
    "Int64ShaderOps": "ArdaBackendFeature::ShaderInt64",
    "DepthBoundsTestSupported": "rendering.depth-bounds-test",
    "CopyQueueTimestampQueriesSupported": "queue.copy-timestamps",
    "CastingFullyTypedFormatSupported": "texture.fully-typed-format-reinterpretation",
    "BarycentricsSupported": "shader.fragment-shader-barycentric",
    "EXISTING_HEAPS.Supported": "memory.open-existing-heap",
    "MSAA64KBAlignedTextureSupported": "texture.msaa-64-kib-alignment",
    "AtomicShaderInstructions": "shader.cross-node-atomics",
    "SRVOnlyTiledResourceTier3": "texture.sparse-3d-sampling",
    "DisplayableTexture": "presentation.displayable-texture",
    "AdditionalShadingRatesSupported": "rendering.additional-shading-rates",
    "PerPrimitiveShadingRateSupportedWithViewportIndexing": "rendering.per-primitive-shading-rate-with-viewport-indexing",
    "ShadingRateImageTileSize": "rendering.shading-rate-image-tile-size",
    "BackgroundProcessingSupported": "queue.background-processing",
    "UnalignedBlockTexturesSupported": "texture.unaligned-compressed-blocks",
    "MeshShaderPipelineStatsSupported": "shader.mesh-pipeline-statistics",
    "MeshShaderSupportsFullRangeRenderTargetArrayIndex": "shader.mesh-full-layer-range",
    "AtomicInt64OnTypedResourceSupported": "shader.atomic-int64.typed-resource",
    "AtomicInt64OnGroupSharedSupported": "shader.atomic-int64.shared",
    "DerivativesInMeshAndAmplificationShadersSupported": "shader.mesh-and-task-derivatives",
    "VariableRateShadingSumCombinerSupported": "rendering.shading-rate-sum-combiner",
    "MeshShaderPerPrimitiveShadingRateSupported": "rendering.variable-rate-shading.mesh-shader",
    "AtomicInt64OnDescriptorHeapResourceSupported": "shader.atomic-int64.descriptor-heap-resource",
    "MSPrimitivesPipelineStatisticIncludesCulledPrimitives": "shader.mesh-statistics-include-culled-primitives",
    "EnhancedBarriersSupported": "synchronization.enhanced-barriers",
    "RelaxedFormatCastingSupported": "texture.relaxed-format-reinterpretation",
    "UnrestrictedBufferTextureCopyPitchSupported": "texture.unrestricted-buffer-copy-pitch",
    "UnrestrictedVertexElementAlignmentSupported": "rendering.unrestricted-vertex-element-alignment",
    "InvertedViewportHeightFlipsYSupported": "rendering.inverted-viewport-height",
    "InvertedViewportDepthFlipsZSupported": "rendering.inverted-viewport-depth",
    "TextureCopyBetweenDimensionsSupported": "texture.cross-dimension-copy",
    "AlphaBlendFactorSupported": "rendering.alpha-blend-factor",
    "AdvancedTextureOpsSupported": "shader.advanced-texture-operations",
    "WriteableMSAATexturesSupported": "texture.multisampled-storage",
    "IndependentFrontAndBackStencilRefMaskSupported": "rendering.independent-front-back-stencil",
    "TriangleFanSupported": "rendering.triangle-fans",
    "DynamicIndexBufferStripCutSupported": "rendering.dynamic-primitive-restart",
    "DynamicDepthBiasSupported": "rendering.dynamic-depth-bias",
    "GPUUploadHeapSupported": "memory.gpu-upload-heap",
    "NonNormalizedCoordinateSamplersSupported": "sampler.unnormalized-coordinates",
    "ManualWriteTrackingResourceSupported": "memory.manual-write-tracking",
    "RenderPassesValid": "rendering.render-pass",
    "MismatchingOutputDimensionsSupported": "rendering.mismatched-output-dimensions",
    "PointSamplingAddressesNeverRoundUp": "sampler.point-address-rounds-down",
    "RasterizerDesc2Supported": "rendering.configurable-line-rasterization",
    "NarrowQuadrilateralLinesSupported": "rendering.narrow-quadrilateral-lines",
    "AnisoFilterWithPointMipSupported": "sampler.anisotropic-point-mip",
    "MaxSamplerDescriptorHeapSize": "descriptor.max-sampler-heap-size",
    "MaxSamplerDescriptorHeapSizeWithStaticSamplers": "descriptor.max-sampler-heap-size-with-static-samplers",
    "MaxViewDescriptorHeapSize": "descriptor.max-resource-heap-size",
    "ComputeOnlyCustomHeapSupported": "memory.compute-only-custom-heap",
    "ComputeOnlyWriteWatchSupported": "memory.compute-only-write-watch",
    "PREDICATION.Supported": "command.predication",
    "HARDWARE_COPY.Supported": "queue.hardware-copy",
    "SampleCmpGradientAndBiasSupported": "shader.comparison-sampling-gradient-and-bias",
    "ExtendedCommandInfoSupported": "command.extended-indirect-info",
    "APPLICATION_SPECIFIC_DRIVER_STATE.Supported": "shader-cache.application-specific-driver-state",
    "BYTECODE_BYPASS_HASH_SUPPORTED.Supported": "shader.bytecode-hash-bypass",
    "ComputeQueuesPer3DQueue": "queue.compute-per-graphics",
    "ShaderExecutionReorderingActuallyReorders": "shader.execution-reordering",
    "CreateByteOffsetViewsSupported": "descriptor.byte-offset-views",
    "Max1DDispatchSize": "compute.max-linear-dispatch-size",
    "Max1DDispatchMeshSize": "shader.max-linear-mesh-dispatch-size",
    "MultisubgraphPartitionsSupported": "compute-graph.multi-subgraph-partitions",
    "CpuBindingSupported": "compute-graph.cpu-binding",
    "SupportedByOS": "diagnostics.gpu-dump-os-support",
    "USER_DEFINED_ANNOTATION.Supported": "diagnostics.user-annotation",
    "ExplicitPsdbsSupported": "shader-cache.explicit-pipeline-state-database",
}


# Thresholds are named SDK values. Public keys describe their individual effect
# and carry a Boolean, never a native tier ordinal or mask.
FEATURE_TESTS = {
    "ConservativeRasterizationTier": [
        ("rendering.conservative-rasterization.hardware-support", ">= D3D12_CONSERVATIVE_RASTERIZATION_TIER_1")],
    "ViewInstancingTier": [("ArdaBackendFeature::Multiview", ">= D3D12_VIEW_INSTANCING_TIER_1")],
    "VariableShadingRateTier": [
        ("rendering.variable-rate-shading.hardware-support", ">= D3D12_VARIABLE_SHADING_RATE_TIER_1"),
        ("rendering.variable-rate-shading.per-draw", ">= D3D12_VARIABLE_SHADING_RATE_TIER_1"),
        ("rendering.variable-rate-shading.per-primitive", ">= D3D12_VARIABLE_SHADING_RATE_TIER_2"),
        ("rendering.variable-rate-shading.attachment", ">= D3D12_VARIABLE_SHADING_RATE_TIER_2")],
    "Native16BitShaderOpsSupported": [
        ("ArdaBackendFeature::ShaderInt16", "!= FALSE")],
    "MeshShaderTier": [("ArdaBackendFeature::TaskShader", ">= D3D12_MESH_SHADER_TIER_1")],
    "WaveMMATier": [("shader.wave-matrix-multiply-accumulate", ">= D3D12_WAVE_MMA_TIER_1_0")],
    "ProgrammableSamplePositionsTier": [
        ("rendering.programmable-sample-locations", ">= D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_1"),
        ("rendering.per-pixel-sample-locations", ">= D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_2")],
    "RecreateAtTier": [("resource.recreate-at-address", ">= D3D12_RECREATE_AT_TIER_1")],
    "ExecuteIndirectTier": [("command.indirect-incrementing-constant", ">= D3D12_EXECUTE_INDIRECT_TIER_1_1")],
    "SupportTier": [("memory.tight-resource-alignment", ">= D3D12_TIGHT_ALIGNMENT_TIER_1")],
    "Impl": [("command.asynchronous", ">= D3D12_ASYNC_COMMANDS_IMPL_FALLBACK"),
             ("command.asynchronous-native", "== D3D12_ASYNC_COMMANDS_IMPL_NATIVE")],
    "PartialGraphicsProgramsTier": [
        ("shader.partial-graphics-program", "!= D3D12_PARTIAL_GRAPHICS_PROGRAMS_TIER_NOT_SUPPORTED")],
    "MinPrecisionSupport": [
        ("shader.relaxed-precision-10-bit", "& D3D12_SHADER_MIN_PRECISION_SUPPORT_10_BIT"),
        ("shader.relaxed-precision-16-bit", "& D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT")],
    "ResourceBindingTier": [
        ("resource.descriptor-indexing", ">= D3D12_RESOURCE_BINDING_TIER_2"),
        ("descriptor.unrestricted-resource-heap", ">= D3D12_RESOURCE_BINDING_TIER_3")],
    "ResourceHeapTier": [
        ("memory.mixed-resource-heap", ">= D3D12_RESOURCE_HEAP_TIER_2")],
    "CrossNodeSharingTier": [
        ("memory.cross-node-sharing", ">= D3D12_CROSS_NODE_SHARING_TIER_1_EMULATED"),
        ("memory.cross-node-native-sharing", ">= D3D12_CROSS_NODE_SHARING_TIER_1")],
    "RenderPassesTier": [
        ("rendering.native-render-pass", ">= D3D12_RENDER_PASS_TIER_1"),
        ("rendering.efficient-render-pass-storage-writes", ">= D3D12_RENDER_PASS_TIER_2")],
    "DumpFileDriverTier": [
        ("diagnostics.gpu-dump.driver-data", ">= D3D12_DUMP_FILE_DRIVER_TIER_1")],
    "D3D12_OPTIONS4.SharedResourceCompatibilityTier": [
        ("resource.cross-api-share-typeless-texture", ">= D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_1"),
        ("format.nv12.cross-api-sharing", ">= D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER_2")],
}

FLAG_FIELDS = {
    "SupportFlags": ("shader-cache", "D3D12_SHADER_CACHE_SUPPORT_FLAGS", "D3D12_SHADER_CACHE_SUPPORT_",
                     {"single-pso": "pipeline-state", "library": "pipeline-library",
                      "automatic-inproc-cache": "automatic-process-cache", "session-desc1": "extended-session-options"}),
    "WriteBufferImmediateSupportFlags": (
        "command.immediate-buffer-write", "D3D12_COMMAND_LIST_SUPPORT_FLAGS", "D3D12_COMMAND_LIST_SUPPORT_FLAG_",
        {"direct": "graphics"}),
    "DumpFileDriverOptionsMask": (
        "diagnostics.gpu-dump", "D3D12_DUMP_FILE_DRIVER_OPTIONS", "D3D12_DUMP_FILE_DRIVER_OPTION_", {}),
}

# Published from final RHI capabilities or exposed through more specific queries.
QUALIFIED_FIELDS = {
    "TiledResourcesTier", "SharedResourceCompatibilityTier", "SharingTier",
    "RaytracingTier", "SamplerFeedbackTier", "WorkGraphsTier", "WaveOps",
    "WaveLaneCountMin", "WaveLaneCountMax",
}

def emit_feature(lines, key, value, indent="        "):
    literal = key if key.startswith("ArdaBackendFeature::") else f'"{key}"'
    lines.append(f"{indent}Features->Add({literal}, static_cast<uint64_t>({value}));")


def format_name(value):
    value = value[len("DXGI_FORMAT_"):]
    if value == "D32_FLOAT_S8X24_UINT":
        return "d32-float-s8-uint"
    value = re.sub(r"^((?:[RGBA]\d+){2,})(?=_)", collapse_component_widths, value)
    return value.lower().replace("_", "-")


def collapse_component_widths(match):
    components = re.findall(r"([RGBA])(\d+)", match[1])
    if len({width for _, width in components}) == 1:
        return "".join(channel for channel, _ in components) + components[0][1]
    return match[1]


def emit_named_array(lines, name, enum, values, to_name):
    lines.extend([f"constexpr TD3D12NamedValue<{enum}> {name}[] = {{",
                  *(f'    {{{value}, "{to_name(value)}"}},' for value in values), "};"])

def rename_token(value, renames):
    value = value.lower().replace("_", "-")
    for source, target in renames.items():
        value = value.replace(source, target)
    return value


def enum_values(header, enum, prefix, excluded=()):
    match = re.search(rf"enum\s+{enum}\s*\{{([^}}]+)\}}", header)
    if not match:
        raise ValueError(f"Missing enum {enum} in Agility SDK headers")
    values = re.findall(rf"\b({prefix}\w+)\s*=", match[1])
    return [value for value in values if value not in excluded]


def emit_array(lines, name, enum, values):
    values = list(values)
    if not values:
        raise ValueError(f"No enum values found for {enum}")
    lines.extend([f"constexpr {enum} {name}[] = {{", *(f"    {value}," for value in values), "};"])


def generate(sdk):
    d3d12 = sdk.read_text(encoding="utf-8-sig")
    dxgi = sdk.with_name("dxgiformat.h").read_text(encoding="utf-8-sig")
    video = sdk.with_name("d3d12video.h").read_text(encoding="utf-8-sig")
    lines = ["// Generated from the Agility SDK headers. Do not edit."]
    emit_named_array(lines, "Formats", "DXGI_FORMAT",
                     enum_values(dxgi, "DXGI_FORMAT", "DXGI_FORMAT_",
                                 {"DXGI_FORMAT_FORCE_UINT", "DXGI_FORMAT_UNKNOWN"}),
                     format_name)
    emit_array(lines, "ShaderModels", "D3D_SHADER_MODEL",
               reversed(enum_values(d3d12, "D3D_SHADER_MODEL", "D3D_SHADER_MODEL_",
                                    {"D3D_SHADER_MODEL_NONE"})))
    emit_array(lines, "RootSignatures", "D3D_ROOT_SIGNATURE_VERSION",
               reversed(enum_values(d3d12, "D3D_ROOT_SIGNATURE_VERSION", "D3D_ROOT_SIGNATURE_VERSION_",
                                    {"D3D_ROOT_SIGNATURE_VERSION_1"})))
    for name, enum, prefix, excluded, renames in (
        ("QueueTypes", "D3D12_COMMAND_LIST_TYPE", "D3D12_COMMAND_LIST_TYPE_",
         {"NONE", "BUNDLE"}, {"direct": "graphics"}),
        ("QueuePriorities", "D3D12_COMMAND_QUEUE_PRIORITY", "D3D12_COMMAND_QUEUE_PRIORITY_", set(), {}),
        ("BarrierLayouts", "D3D12_BARRIER_LAYOUT", "D3D12_BARRIER_LAYOUT_",
         {"PRESENT"}, {"direct-queue": "graphics-queue"}),
        ("MultisampleFlags", "D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS",
         "D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_", set(), {"none": "regular", "tiled-resource": "sparse"}),
    ):
        emit_named_array(lines, name, enum,
                         enum_values(d3d12, enum, prefix, {prefix + item for item in excluded}),
                         lambda value: rename_token(value[len(prefix):], renames))
    for name, suffix in (("VideoCodecs", "CODEC"), ("H264Profiles", "PROFILE_H264"),
                         ("HevcProfiles", "PROFILE_HEVC"), ("Av1Profiles", "AV1_PROFILE"),
                         ("VideoRateControls", "RATE_CONTROL_MODE")):
        enum = "D3D12_VIDEO_ENCODER_" + suffix
        emit_named_array(lines, name, enum, enum_values(video, enum, enum + "_"),
                         lambda value: rename_token(value[len(enum) + 1:], {}))

    for array, suffix in (("H264Levels", "LEVELS_H264"), ("HevcLevels", "LEVELS_HEVC"),
                          ("Av1Levels", "AV1_LEVELS")):
        enum = "D3D12_VIDEO_ENCODER_" + suffix
        lines.append(f"constexpr TD3D12VideoLevel<{enum}> {array}[] = {{")
        for value in enum_values(video, enum, enum + "_"):
            version = value[len(enum) + 1:].replace("_", "")
            if version == "1b":
                lines.append(f"    {{{value}, 105}},")
            elif version.isdigit():
                number = int(version) * (100 if len(version) == 1 else 10)
                lines.append(f"    {{{value}, {number}}},")
        lines.append("};")
    decode_profiles = re.findall(r"DEFINE_GUID\(\s*D3D12_VIDEO_DECODE_PROFILE_(\w+),\s*([^)]+)\)", video)
    lines.append("constexpr TD3D12NamedValue<GUID> VideoDecodeProfiles[] = {")
    for profile, guid in decode_profiles:
        lines.append(f'    {{{{{guid}}}, "{rename_token(profile, {})}"}},')
    lines.append("};")
    hardware_session = re.search(
        r"DEFINE_GUID\(\s*D3D12_PROTECTED_RESOURCES_SESSION_HARDWARE_PROTECTED,\s*([^)]+)\)", d3d12)
    if not hardware_session:
        raise ValueError("Missing hardware-protected session GUID")
    lines.append(f"constexpr GUID HardwareProtectedSession{{{hardware_session[1]}}};")
    selectors = enum_values(d3d12, "D3D12_FEATURE", "D3D12_FEATURE_")
    structs = dict(re.findall(r"\bstruct\s+D3D12_FEATURE_DATA_(\w+)\s*\{([^}]+)\}", d3d12))
    aliases = {"D3D12_TIGHT_ALIGNMENT": "TIGHT_ALIGNMENT"}
    for selector in selectors:
        name = selector[len("D3D12_FEATURE_"):]
        if name in SPECIAL_SELECTORS:
            continue
        struct = aliases.get(name, name)
        if struct not in structs:
            raise ValueError(f"No data structure for {selector}; classify its query explicitly")
        lines.extend(["{", f"    D3D12_FEATURE_DATA_{struct} Data{{}};", f"    if (Probe.Query({selector}, Data))", "    {"])
        for declaration in structs[struct].split(";"):
            if not declaration.strip():
                continue
            field = re.fullmatch(r"\s*(?:_Out_\s+)?(\w+)\s+(\w+)\s*", declaration)
            if not field:
                raise ValueError(f"Classify {selector} field explicitly: {declaration.strip()}")
            field_type, member = field.groups()
            if name == "D3D12_OPTIONS" and member == "MaxGPUVirtualAddressBitsPerResource":
                continue  # Superseded by GPU_VIRTUAL_ADDRESS_SUPPORT.
            key = FEATURE_NAMES.get(f"{name}.{member}", FEATURE_NAMES.get(member))
            if key:
                value = f"Data.{member} != FALSE" if field_type == "BOOL" else f"Data.{member}"
                if field_type == "D3D12_TRI_STATE":
                    lines.extend([f"        if (Data.{member} != D3D12_TRI_STATE_UNKNOWN)", "        {"])
                    emit_feature(lines, key, value, "            ")
                    lines.append("        }")
                else:
                    emit_feature(lines, key, value)
            tests = FEATURE_TESTS.get(f"{name}.{member}", FEATURE_TESTS.get(member, []))
            for key, test in tests:
                value = f"Data.{member} {test}"
                if test.startswith("&"):
                    value = f"({value}) != 0"
                emit_feature(lines, key, value)
            if member in FLAG_FIELDS:
                domain, enum, prefix, renames = FLAG_FIELDS[member]
                for flag in enum_values(d3d12, enum, prefix, {prefix + "NONE"}):
                    name_suffix = rename_token(flag[len(prefix):], renames)
                    emit_feature(lines, f"{domain}.{name_suffix}", f"(Data.{member} & {flag}) != 0")
            if member == "SupportedSampleCountsWithNoOutputs":
                for count in (1, 2, 4, 8, 16, 32):
                    emit_feature(lines, f"rendering.attachmentless.samples-{count}",
                                 f"(Data.{member} & {count}) != 0")
            elif not key and not tests and member not in FLAG_FIELDS and member not in QUALIFIED_FIELDS:
                raise ValueError(f"Map or explicitly exclude {selector}.{member}")
        lines.extend(["    }", "}"])
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sdk_header, destination = map(Path, sys.argv[1:])
    generated = generate(sdk_header)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(generated, encoding="utf-8")
