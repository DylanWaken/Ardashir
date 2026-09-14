#!/usr/bin/env python3
"""Check the Arda stem on named types in project-owned C++, CUDA and shaders.

This source check complements compilation. It includes local/nested declarations,
aliases and Arda type-declaring macros, without treating template parameters,
comments, string literals or SDK imports as project type definitions.
"""

import argparse
from dataclasses import dataclass
from pathlib import Path
import re

from FormatCode import source_files


IDENTIFIER = r"[A-Za-z_]\w*"
QUALIFIED = rf"{IDENTIFIER}(?:\s*::\s*{IDENTIFIER})*"
PREFIX = re.compile(r"^[FITE]Arda[A-Z0-9]\w*$")
LEXICAL_NOISE = re.compile(
    r'R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    r"|//[^\n]*|/\*.*?\*/", re.DOTALL)
TYPE_MACROS = (
    "ARDA_BEGIN_SHADER_PARAMETER_STRUCT",
    "ARDA_BEGIN_COMPUTE_PARAMETER_STRUCT",
    "ARDA_CUDA_PARAMETER_STRUCT",
    "ARDA_SHADER_PERMUTATION_BOOL",
    "ARDA_SHADER_PERMUTATION_INT",
)
MACRO_PARAMETER = "FArdaTypeCheckMacroParameter"
MACRO_BOUNDARY = "\0"


@dataclass(frozen=True)
class FArdaTypeDeclaration:
    name: str
    line: int
    kind: str


def blank(text):
    return "".join("\n" if character == "\n" else " " for character in text)


def balanced_end(text, start, opening, closing):
    """Return the first offset after a balanced group in already-masked source."""
    depth = 0
    for index in range(start, len(text)):
        depth += (text[index] == opening) - (text[index] == closing)
        if depth == 0:
            return index + 1
    return None


def mask_groups(text, pattern, opening, closing):
    """Mask a prefix and its balanced group while preserving source line numbers."""
    characters = list(text)
    for match in re.finditer(pattern, text):
        end = balanced_end(text, match.end(), opening, closing)
        if end is not None:
            characters[match.start():end] = blank(text[match.start():end])
    return "".join(characters)


def mask_source(source):
    text = LEXICAL_NOISE.sub(lambda match: blank(match.group()), source)
    text = mask_groups(text, r"\balignas\s*(?=\()", "(", ")")
    text = mask_groups(text, r"(?=\[\[)", "[", "]")
    # Keep macro bodies so fixed helper declarations are checked, but a macro's
    # formal parameters do not themselves declare concrete project type names.
    directive = re.compile(r"^[ \t]*#(?:[^\n\\]|\\[^\n]|\\\n)*", re.MULTILINE)

    def mask_directive(match):
        value = match.group()
        definition = re.match(r"\s*#\s*define\s+" + IDENTIFIER + r"(?:\(([^)]*)\))?", value)
        if not definition:
            return blank(value)
        body = value[definition.end():]
        for parameter in re.findall(IDENTIFIER, definition.group(1) or ""):
            # A valid placeholder keeps `struct Name final` from looking like `struct final`.
            body = re.sub(r"\b" + parameter + r"\b", MACRO_PARAMETER, body)
        # Some parameter macros deliberately end with an unfinished typedef. Do not join it
        # to an unrelated declaration in the next macro's body.
        return blank(value[:definition.end()]) + body + MACRO_BOUNDARY

    text = directive.sub(mask_directive, text)
    text = text.replace("\\\n", " \n")
    return mask_groups(text, r"\btemplate\s*(?=<)", "<", ">")


def typedef_parts(text, start):
    """Yield each declarator, stopping at a top-level semicolon or macro boundary."""
    closers = {"(": ")", "[": "]", "{": "}"}
    stack = []
    first = start
    for index in range(start, len(text)):
        character = text[index]
        if character == MACRO_BOUNDARY:
            return
        if character in closers:
            stack.append(closers[character])
        elif character == "<" and (not stack or stack[-1] == ">"):
            stack.append(">")
        elif stack and character == stack[-1]:
            stack.pop()
        elif not stack and character in ",;":
            yield first, text[first:index]
            first = index + 1
            if character == ";":
                return


def typedef_name(part):
    """Find the alias declarator without mistaking aggregate fields for aliases."""
    text = mask_groups(part, r"(?=\{)", "{", "}")
    text = mask_groups(text, r"\b(?:decltype|typeof|__typeof__)\s*(?=\()", "(", ")")
    text = mask_groups(text, r"(?=<)", "<", ">")
    text = mask_groups(text, r"(?=\[)", "[", "]")
    text = re.sub(r"##\s*" + MACRO_PARAMETER, lambda match: blank(match.group()), text)
    parenthesis = text.find("(")
    if parenthesis >= 0:
        end = balanced_end(text, parenthesis, "(", ")")
        group = text[parenthesis:end] if end is not None else ""
        pointer = re.fullmatch(
            rf"\(\s*(?P<convention>[A-Z_]\w*\s+)?(?:{QUALIFIED}\s*::\s*)?"
            rf"[*&]+\s*(?:(?:const|volatile)\s+)*(?P<name>{IDENTIFIER})\s*\)", group)
        # An identifier before `*` may be a calling-convention macro, but in `Function(Type *Arg)`
        # it is an ordinary parameter type. A convention belongs to a following function group.
        if pointer and (not pointer.group("convention") or text[end:].lstrip().startswith("(")):
            return pointer.group("name"), parenthesis + pointer.start("name")
        # A direct function typedef places its alias immediately before its parameter list.
        text = text[:parenthesis]
    names = list(re.finditer(IDENTIFIER, text))
    return (names[-1].group(), names[-1].start()) if names else None


def declarations(source):
    text = mask_source(source)
    found = {}

    def add(name, offset, kind):
        name = re.split(r"\s*::\s*", name)[-1]
        if name == MACRO_PARAMETER:
            return
        line = text.count("\n", 0, offset) + 1
        found[(line, name)] = FArdaTypeDeclaration(name, line, kind)

    named_type = re.compile(
        r"\b(enum\s+(?:class|struct)|enum|class|struct|union)\s+" +
        rf"({QUALIFIED})\s*(?=final\b|[:;{{<#])", re.DOTALL)
    for match in named_type.finditer(text):
        # GLFW owns this ABI name. Window wrappers forward-declare it to avoid
        # including the third-party header in project headers.
        if match.group(2) == "GLFWwindow" and text[match.end():].startswith(";"):
            continue
        add(match.group(2), match.start(2), match.group(1))
    for match in re.finditer(rf"\busing\s+({IDENTIFIER})\s*=", text):
        add(match.group(1), match.start(1), "alias")
    for match in re.finditer(r"\btypedef\b", text):
        for offset, part in typedef_parts(text, match.end()):
            alias = typedef_name(part)
            if alias:
                add(alias[0], offset + alias[1], "typedef")
    macro_names = "|".join(TYPE_MACROS)
    for match in re.finditer(rf"\b(?:{macro_names})\s*\(\s*({IDENTIFIER})", text):
        add(match.group(1), match.start(1), "macro type")
    return sorted(found.values(), key=lambda item: (item.line, item.name))


def violations(source):
    return [declaration for declaration in declarations(source)
            if not PREFIX.fullmatch(declaration.name)]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path,
                        help="Repository-relative source files/directories; default: authored source inventory.")
    arguments = parser.parse_args(argv)
    paths = source_files(arguments.paths)
    count = 0
    errors = 0
    for path in paths:
        items = declarations(path.read_text(encoding="utf-8-sig"))
        count += len(items)
        for item in items:
            if not PREFIX.fullmatch(item.name):
                errors += 1
                print(f"{path}:{item.line}: {item.kind} {item.name} requires FArda, IArda, EArda or TArda.")
    print(f"Checked {count} named declarations in {len(paths)} authored source files: {errors} violation(s).")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
