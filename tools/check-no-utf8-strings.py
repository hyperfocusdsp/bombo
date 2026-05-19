#!/usr/bin/env python3
"""Fail if any C/C++ string literal in Source/ contains non-ASCII bytes.

JUCE's juce::String(const char*) constructor explicitly disallows bytes
above 0x7F (see juce_String.h: "must not contain any characters with a
value above 127"). Passing UTF-8 box-drawing or punctuation through it
silently produces mojibake at render time. This script catches the
violation before the build.

Three failure modes detected:
  1. Raw non-ASCII bytes in the source file (e.g. `·` typed directly).
  2. \\xNN hex escape with NN >= 0x80 (compiles to a non-ASCII byte).
  3. \\uNNNN / \\UNNNNNNNN unicode escape with codepoint >= 0x80.

Comments are skipped. To embed Unicode intentionally, use the codepoint
via juce::String::fromUTF8(R"raw(...)raw") -- not a bare "..." literal --
and add the file to ALLOWED_FILES below with the reason.
"""
from __future__ import annotations
import os, re, sys

ROOTS = [
    os.path.join(os.path.dirname(__file__), "..", "Source"),
    os.path.join(os.path.dirname(__file__), "..", "tests"),
]
EXTS = (".cpp", ".h")

# Files that legitimately use juce::String::fromUTF8 / CharPointer_UTF8.
# Empty for now; ASCII-only is the project rule.
ALLOWED_FILES: set[str] = set()


_ESCAPE_RE = re.compile(
    r"\\x([0-9A-Fa-f]+)"        # \xNN  (greedy hex run)
    r"|\\u([0-9A-Fa-f]{4})"     # \uNNNN
    r"|\\U([0-9A-Fa-f]{8})"     # \UNNNNNNNN
)


def literal_has_non_ascii(lit: str) -> bool:
    """True if the C/C++ string-literal source (including surrounding quotes
    but not yet decoded) contains any byte or escape that compiles to >= 0x80."""
    # 1. Raw non-ASCII characters typed into source.
    if any(ord(ch) > 127 for ch in lit):
        return True
    # 2. Hex / unicode escapes whose value is >= 0x80.
    for m in _ESCAPE_RE.finditer(lit):
        hex_part = next((g for g in m.groups() if g is not None), None)
        if hex_part is None:
            continue
        # Only the first 2 hex digits are consumed for \x in C++, but
        # the lexer accepts any-length run -- be conservative and treat
        # any value above 0x7F as a violation.
        if int(hex_part, 16) > 0x7F:
            return True
    return False


def scan(path: str) -> list[tuple[int, str]]:
    """Return (lineno, literal) pairs for non-ASCII string literals
    outside C/C++ comments. Handles // and /* */ comments, escape
    sequences inside strings, and raw-string literals R"(...)".
    """
    with open(path, "rb") as f:
        data = f.read()
    text = data.decode("utf-8", errors="surrogateescape")

    violations: list[tuple[int, str]] = []
    i, n = 0, len(text)
    line = 1
    in_line_comment = False
    in_block_comment = False
    in_string = False
    in_raw = False
    raw_delim = ""
    string_start = -1
    string_line = -1

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if c == "\n":
            line += 1
            in_line_comment = False
            i += 1
            continue

        if in_line_comment:
            i += 1
            continue
        if in_block_comment:
            if c == "*" and nxt == "/":
                in_block_comment = False
                i += 2
                continue
            i += 1
            continue
        if in_raw:
            if c == ")" and text[i + 1 : i + 1 + len(raw_delim) + 1] == raw_delim + '"':
                lit = text[string_start : i + 1 + len(raw_delim) + 1]
                if literal_has_non_ascii(lit):
                    violations.append((string_line, lit))
                in_raw = False
                i += 1 + len(raw_delim) + 1
                continue
            i += 1
            continue
        if in_string:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                lit = text[string_start : i + 1]
                if literal_has_non_ascii(lit):
                    violations.append((string_line, lit))
                in_string = False
                i += 1
                continue
            i += 1
            continue

        # Not in any literal/comment context.
        if c == "/" and nxt == "/":
            in_line_comment = True
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block_comment = True
            i += 2
            continue
        # Raw string literal: R"delim(...)delim"  (also LR/uR/u8R/U variants).
        if c == "R" or (c in "Lu" and nxt == "R") or (text[i : i + 3] == "u8R"):
            # find R then "
            r_pos = text.find('R"', i, i + 4)
            if r_pos != -1 and r_pos < i + 4:
                paren = text.find("(", r_pos + 2)
                if paren != -1:
                    raw_delim = text[r_pos + 2 : paren]
                    in_raw = True
                    string_start = i
                    string_line = line
                    i = paren + 1
                    continue
        if c == '"':
            in_string = True
            string_start = i
            string_line = line
            i += 1
            continue
        i += 1

    return violations


def main() -> int:
    bad = 0
    project_root = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
    for root in ROOTS:
        for dirpath, _, files in os.walk(root):
            for f in files:
                if not f.endswith(EXTS):
                    continue
                path = os.path.normpath(os.path.join(dirpath, f))
                rel = os.path.relpath(path, project_root)
                if rel in ALLOWED_FILES:
                    continue
                violations = scan(path)
                for lineno, lit in violations:
                    bad += 1
                    preview = lit if len(lit) < 80 else lit[:77] + "..."
                    print(f"{rel}:{lineno}: non-ASCII in string literal: {preview}")
    if bad:
        print(
            f"\n{bad} violation(s). JUCE String(const char*) silently corrupts "
            f"bytes above 0x7F. Use ASCII or add the file to ALLOWED_FILES "
            f"in {os.path.relpath(__file__)} with juce::String::fromUTF8.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
