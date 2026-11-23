"""Generate Vulkan's physical-device feature queries from Vulkan-Headers' vk.xml."""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from VulkanFeatureVocabulary import BOOLEAN_CAPABILITIES, DEVICE_CONTRACT_CAPABILITIES, EXTENSION_CAPABILITIES


COMMON_NAMES = {
    "shader.float16": "ShaderFloat16",
    "shader.float64": "ShaderFloat64",
    "shader.int16": "ShaderInt16",
    "shader.int64": "ShaderInt64",
    "shader.geometry": "GeometryShader",
    "shader.tessellation": "TessellationShader",
    "shader.mesh": "MeshShader",
    "shader.task": "TaskShader",
    "ray-tracing.pipeline": "RayTracingPipeline",
    "ray-tracing.inline-query": "InlineRayQuery",
    "ray-tracing.acceleration-structure": "AccelerationStructure",
    "rendering.conservative-rasterization": "ConservativeRasterization",
    "rendering.multiview": "Multiview",
    "resource.sparse-binding": "SparseBinding",
    "synchronization.timeline-semaphore": "TimelineSemaphore",
}


def includes_vulkan(node, attribute="api"):
    return "vulkan" in node.get(attribute, "vulkan").split(",")


def dependency_expression(dependency, device_extensions):
    terms = []
    for token in re.findall(r"!?VK_\w+|[(),+]|\S+", dependency):
        negate = token.startswith("!")
        token = token.lstrip("!")
        version = re.fullmatch(r"VK_(?:BASE_|COMPUTE_|GRAPHICS_)?VERSION_(\d+_\d+)", token)
        if version:
            operator = "<" if negate else ">="
            terms.append(f"ApiVersion {operator} VK_API_VERSION_{version[1]}")
        elif token in device_extensions:
            operator = "!" if negate else ""
            terms.append(f'{operator}HasExtension("{token}")')
        elif token in ("(", ")", ",", "+"):
            terms.append({",": " || ", "+": " && "}.get(token, token))
        else:
            raise ValueError(f"Unhandled Vulkan feature requirement: {token}")
    return "".join(terms)


def key_expression(key):
    word = r"[a-z0-9]+(?:-[a-z0-9]+)*"
    if not key[0].isalpha() or not re.fullmatch(rf"{word}(?:\.{word})+", key):
        raise ValueError(f"Invalid Arda capability name: {key}")
    if key in COMMON_NAMES:
        return f"ArdaBackendFeature::{COMMON_NAMES[key]}"
    return f'"{key}"'


def header_version(registry):
    defines = {
        node.findtext("name"): "".join(node.itertext())
        for node in registry.findall("types/type[@category='define']")
        if includes_vulkan(node)
    }
    patch = re.search(r"#define\s+VK_HEADER_VERSION\s+(\d+)", defines.get("VK_HEADER_VERSION", ""))
    api = re.search(
        r"VK_MAKE_API_VERSION\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*VK_HEADER_VERSION\s*\)",
        defines.get("VK_HEADER_VERSION_COMPLETE", ""),
    )
    if not patch or not api:
        raise ValueError("Vulkan header version is missing from vk.xml")
    return ", ".join((*api.groups(), patch[1]))


def generate(registry):
    types = {
        node.get("name"): node for node in registry.findall("types/type")
        if node.get("category") == "struct" and includes_vulkan(node)
    }
    features = {
        name: node for name, node in types.items()
        if "VkPhysicalDeviceFeatures2" in node.get("structextends", "").split(",")
    }

    def canonical_name(name):
        while name in types and types[name].get("alias"):
            name = types[name].get("alias")
        return name

    extensions = [
        node for node in registry.findall("extensions/extension")
        if includes_vulkan(node, "supported")
    ]
    device_extensions = {node.get("name") for node in extensions if node.get("type") == "device"}
    requirements = defaultdict(set)
    sources = registry.findall("feature") + extensions
    for source in sources:
        if not includes_vulkan(source):
            continue
        name = source.get("name")
        for require in source.findall("require"):
            if not includes_vulkan(require):
                continue
            for used in require.findall("type"):
                if not includes_vulkan(used):
                    continue
                canonical = canonical_name(used.get("name"))
                if canonical not in features:
                    continue
                if source.tag == "feature":
                    version = source.get("number").replace(".", "_")
                    guard = f"VK_VERSION_{version}"
                    condition = f"ApiVersion >= VK_API_VERSION_{version}"
                elif name in device_extensions:
                    guard = name
                    condition = f'HasExtension("{name}")'
                else:
                    raise ValueError(f"Physical device feature {canonical} comes from instance extension {name}")
                if require.get("depends"):
                    dependency = dependency_expression(require.get("depends"), device_extensions)
                    condition += f" && ({dependency})"
                requirements[canonical].add((guard, condition))

    lines = [
        "// Generated from Vulkan-Headers' vk.xml. Do not edit.",
        f"static_assert(VK_HEADER_VERSION_COMPLETE == VK_MAKE_API_VERSION({header_version(registry)}),",
        '    "The Vulkan headers and vk.xml must have the same version.");',
    ]

    def emit_fields(type_name, value):
        field_lines = []
        for member in types[type_name].findall("member"):
            if not includes_vulkan(member):
                continue
            field = member.findtext("name")
            if field in ("sType", "pNext"):
                continue
            if member.findtext("type") != "VkBool32":
                raise ValueError(f"Expected VkBool32 for {type_name}.{field}")
            if field not in BOOLEAN_CAPABILITIES:
                raise ValueError(
                    f"No Arda capability mapping for {type_name}.{field}; classify it in VulkanFeatureVocabulary.py"
                )
            key = BOOLEAN_CAPABILITIES[field]
            if key is not None and key not in DEVICE_CONTRACT_CAPABILITIES:
                field_lines.append(f"Report({key_expression(key)}, {value}.{field} == VK_TRUE);")
        return field_lines

    lines.extend(emit_fields("VkPhysicalDeviceFeatures", "CoreFeatures"))
    for name, node in sorted(features.items()):
        # Registry entries outside Vulkan can share structextends without any Vulkan requirement.
        if name not in requirements:
            continue
        field_lines = emit_fields(name, "Value")
        if not field_lines:
            continue
        structure_type = next(member.get("values") for member in node.findall("member")
                              if member.findtext("name") == "sType")
        guards = sorted({guard for guard, _ in requirements[name]})
        conditions = sorted({condition for _, condition in requirements[name]})
        lines += [
            "#if " + " || ".join(f"defined({guard})" for guard in guards),
            "if (" + " || ".join(f"({condition})" for condition in conditions) + ")",
            "{",
            f"{name} Value{{}};",
            f"Value.sType = {structure_type};",
            "Query(&Value);",
            *field_lines,
            "}",
            "#endif",
        ]
    extensions_without_bits = {e.get("name") for e in extensions if e.get("nofeatures") == "true"}
    for key, routes in EXTENSION_CAPABILITIES.items():
        if key in DEVICE_CONTRACT_CAPABILITIES:
            continue
        for route in routes:
            for source in re.findall(r"VK_\w+", route):
                if source in device_extensions and source not in extensions_without_bits:
                    raise ValueError(f"Use the Vulkan feature bits from {source} to report {key}")
        condition = " || ".join(f"({dependency_expression(route, device_extensions)})" for route in routes)
        lines += [f"if ({condition})", "{", f"Report({key_expression(key)}, true);", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    text = generate(ET.parse(args.registry).getroot())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
