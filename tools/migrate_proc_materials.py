#!/usr/bin/env python3
"""One-off migration from compiled procedural materials to ProcMaterial YAML.

The tool intentionally uses only Python's standard library.  It reads the
literal constexpr tables in MaterialRegistry.h, applies Game.yaml's optional
Materials overrides, writes the built-in ProcMaterial catalog, and rewrites
legacy materialIndex/materialDef level fields to stable Sub-material ids.
After all inputs have been read it removes Materials from each supplied game
configuration.
"""

from __future__ import annotations

import argparse
import dataclasses
import pathlib
import re
import sys
from collections.abc import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_REGISTRY = ROOT / "src/BooleanWorld/common/include/common/MaterialRegistry.h"
DEFAULT_LEVEL = ROOT / "src/BooleanWorld/app/resources/world-test-1.yaml"
DEFAULT_OUTPUT = ROOT / "src/BooleanWorld/app/resources/proc-materials-built-in.yaml"
DEFAULT_CONFIGS = [
    ROOT / f"src/Launcher/support/ASTRALEMPRESS/{configuration}/Game.yaml"
    for configuration in ("Debug", "MemCheck", "Release")
]

FLOAT = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"


@dataclasses.dataclass
class Parameter:
    name: str
    minimum: float
    maximum: float
    default: float


@dataclasses.dataclass
class Technique:
    name: str
    colour: tuple[float, float, float]
    parameters: list[Parameter]


@dataclasses.dataclass(frozen=True)
class MaterialValue:
    material_index: int
    params: tuple[float, ...]
    colour: tuple[float, float, float]


def number(text: str) -> float:
    return float(text.rstrip("fF"))


def yaml_number(value: float) -> str:
    # Nine significant digits are enough to round-trip a binary32 value and
    # keep generated data readable. Always retain a decimal point for floats.
    result = format(value, ".9g")
    if "e" not in result.lower() and "." not in result:
        result += ".0"
    return result


def quoted(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def slug(text: str) -> str:
    value = re.sub(r"[^a-z0-9]+", ".", text.lower()).strip(".")
    if not value:
        raise ValueError(f"Cannot make an id from material name {text!r}")
    return value


def parse_registry(path: pathlib.Path) -> list[Technique]:
    text = path.read_text(encoding="utf-8")
    names_start = text.index("MaterialNames")
    params_start = text.index("MaterialParams", names_start)
    names_text = text[names_start:params_start]
    name_pattern = re.compile(
        rf'\{{"([^"]+)",\s*(\d+),\s*\{{\s*({FLOAT})f?,\s*({FLOAT})f?,\s*({FLOAT})f?\s*\}}\}}'
    )
    definitions = name_pattern.findall(names_text)
    if not definitions:
        raise ValueError(f"No MaterialNames entries found in {path}")

    parameter_pattern = re.compile(
        rf'\{{"([^"]+)",\s*({FLOAT})f?,\s*({FLOAT})f?,\s*({FLOAT})f?\}}'
    )
    raw_parameters = parameter_pattern.findall(text[params_start:])

    techniques: list[Technique] = []
    cursor = 0
    for name, count_text, red, green, blue in definitions:
        count = int(count_text)
        selected = raw_parameters[cursor : cursor + count]
        if len(selected) != count:
            raise ValueError(f"Technique {name!r} declares {count} parameters but the table ended early")
        cursor += count
        techniques.append(
            Technique(
                name=name,
                colour=(number(red), number(green), number(blue)),
                parameters=[Parameter(param_name, number(low), number(high), number(default))
                            for param_name, low, high, default in selected],
            )
        )
    if cursor != len(raw_parameters):
        raise ValueError(f"Found {len(raw_parameters) - cursor} unclaimed MaterialParams entries in {path}")
    return techniques


def parse_game_overrides(path: pathlib.Path) -> dict[str, dict[str, dict[str, float]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    try:
        start = next(i for i, line in enumerate(lines) if re.fullmatch(r"\s{2}Materials:\s*", line))
    except StopIteration:
        return {}

    overrides: dict[str, dict[str, dict[str, float]]] = {}
    material_name: str | None = None
    parameter_name: str | None = None
    values: dict[str, float] = {}

    def commit_parameter() -> None:
        nonlocal parameter_name, values
        if parameter_name is None:
            return
        if material_name is None:
            raise ValueError(f"Materials parameter outside a material in {path}: {parameter_name!r}")
        # Match MaterialDefaultsFile: each min/max/default field is optional
        # and an omitted field retains the corresponding compiled value.
        overrides.setdefault(material_name, {})[parameter_name] = dict(values)
        parameter_name = None
        values = {}

    for line in lines[start + 1 :]:
        if line.strip() and not line.lstrip().startswith("#") and len(line) - len(line.lstrip()) <= 2:
            break
        match = re.fullmatch(r"\s{4}- name:\s*(.+?)\s*", line)
        if match:
            commit_parameter()
            material_name = match.group(1).strip('"\'')
            continue
        match = re.fullmatch(r"\s{8}- name:\s*(.+?)\s*", line)
        if match:
            commit_parameter()
            parameter_name = match.group(1).strip('"\'')
            continue
        match = re.fullmatch(rf"\s{{10}}(min|max|default):\s*({FLOAT})\s*", line)
        if match and parameter_name is not None:
            values[match.group(1)] = number(match.group(2))
    commit_parameter()
    return overrides


def apply_overrides(techniques: list[Technique], overrides: dict[str, dict[str, dict[str, float]]]) -> None:
    by_name = {technique.name: technique for technique in techniques}
    unknown_materials = set(overrides) - set(by_name)
    if unknown_materials:
        raise ValueError(f"Game.yaml overrides unknown material(s): {sorted(unknown_materials)}")
    for material_name, parameters in overrides.items():
        technique = by_name[material_name]
        by_parameter = {parameter.name: i for i, parameter in enumerate(technique.parameters)}
        unknown_parameters = set(parameters) - set(by_parameter)
        if unknown_parameters:
            raise ValueError(f"Game.yaml overrides unknown {material_name} parameter(s): {sorted(unknown_parameters)}")
        for parameter_name, override in parameters.items():
            index = by_parameter[parameter_name]
            compiled = technique.parameters[index]
            merged = Parameter(
                parameter_name,
                override.get("min", compiled.minimum),
                override.get("max", compiled.maximum),
                override.get("default", compiled.default),
            )
            if merged.minimum > merged.maximum or not merged.minimum <= merged.default <= merged.maximum:
                raise ValueError(f"Game.yaml gives {material_name}.{parameter_name} invalid bounds/default")
            technique.parameters[index] = merged


def legacy_material_pattern() -> re.Pattern[str]:
    return re.compile(
        rf"^(?P<indent>[ ]+)(?P<field>floorMaterial|ceilingMaterial|wallMaterial):\r?\n"
        rf"(?P=indent)  materialIndex:\s*(?P<index>\d+)\r?\n"
        rf"(?P=indent)  materialDef:\r?\n"
        rf"(?P=indent)    params:\s*\[(?P<params>[^\]]*)\]\r?\n"
        rf"(?P=indent)    baseColour:\s*\[(?P<colour>[^\]]*)\][ \t]*(?=\r?$)",
        re.MULTILINE,
    )


def parse_vector(text: str) -> tuple[float, ...]:
    if not text.strip():
        return ()
    return tuple(number(component.strip()) for component in text.split(","))


def collect_level_materials(paths: Iterable[pathlib.Path], techniques: list[Technique]) -> list[MaterialValue]:
    result: list[MaterialValue] = []
    seen: set[MaterialValue] = set()
    pattern = legacy_material_pattern()
    for path in paths:
        text = path.read_text(encoding="utf-8")
        matches = list(pattern.finditer(text))
        raw_count = len(re.findall(r"\bmaterialIndex:\s*\d+", text))
        if raw_count != len(matches):
            raise ValueError(f"{path}: found {raw_count} materialIndex fields but only {len(matches)} complete legacy material fields")
        for match in matches:
            value = MaterialValue(
                int(match.group("index")), parse_vector(match.group("params")), parse_vector(match.group("colour"))  # type: ignore[arg-type]
            )
            if value.material_index >= len(techniques):
                raise ValueError(f"{path}: materialIndex {value.material_index} is out of range")
            technique = techniques[value.material_index]
            if len(value.params) != len(technique.parameters):
                raise ValueError(
                    f"{path}: {technique.name} material has {len(value.params)} params, expected {len(technique.parameters)}")
            for parameter, parameter_value in zip(technique.parameters, value.params):
                if not parameter.minimum <= parameter_value <= parameter.maximum:
                    raise ValueError(f"{path}: {technique.name}.{parameter.name} is out of bounds")
            if len(value.colour) != 3:
                raise ValueError(f"{path}: a baseColour does not have three components")
            if value not in seen:
                seen.add(value)
                result.append(value)
    return result


def migrated_ids(values: list[MaterialValue], techniques: list[Technique]) -> dict[MaterialValue, str]:
    counters: dict[str, int] = {}
    result: dict[MaterialValue, str] = {}
    for value in values:
        technique_slug = slug(techniques[value.material_index].name)
        counters[technique_slug] = counters.get(technique_slug, 0) + 1
        result[value] = f"migrated.{technique_slug}.{counters[technique_slug]}"
    return result


def render_catalog(techniques: list[Technique], migrated: list[MaterialValue], ids: dict[MaterialValue, str]) -> str:
    lines = [
        '# Generated by tools/migrate_proc_materials.py; edit through the ProcMaterial authoring UI.',
        'program3d: "World/WorldProgram"',
        'program2d: "World/WorldHorizontal2dProgram"',
        "techniqueSchemas:",
    ]
    for index, technique in enumerate(techniques):
        lines += [f"  - materialIndex: {index}", "    parameters:"]
        if not technique.parameters:
            lines[-1] += " []"
        else:
            for parameter in technique.parameters:
                lines += [
                    f"      - name: {quoted(parameter.name)}",
                    f"        min: {yaml_number(parameter.minimum)}",
                    f"        max: {yaml_number(parameter.maximum)}",
                    f"        default: {yaml_number(parameter.default)}",
                ]
    lines.append("subMaterials:")
    for index, technique in enumerate(techniques):
        lines += [
            f"  - id: {quoted('builtin.' + slug(technique.name))}",
            f"    name: {quoted(technique.name)}",
            f"    materialIndex: {index}",
            "    params: [" + ", ".join(yaml_number(p.default) for p in technique.parameters) + "]",
            "    baseColour: [" + ", ".join(yaml_number(v) for v in technique.colour) + "]",
        ]
    for value in migrated:
        technique = techniques[value.material_index]
        ordinal = ids[value].rsplit(".", 1)[1]
        lines += [
            f"  - id: {quoted(ids[value])}",
            f"    name: {quoted('Migrated ' + technique.name + ' ' + ordinal)}",
            f"    materialIndex: {value.material_index}",
            "    params: [" + ", ".join(yaml_number(v) for v in value.params) + "]",
            "    baseColour: [" + ", ".join(yaml_number(v) for v in value.colour) + "]",
        ]
    return "\n".join(lines) + "\n"


def rewrite_level(path: pathlib.Path, ids: dict[MaterialValue, str]) -> int:
    # Read/write bytes so a one-line schema migration does not normalize the
    # rest of an existing CRLF-authored level file.
    text = path.read_bytes().decode("utf-8")
    pattern = legacy_material_pattern()
    count = 0

    def replacement(match: re.Match[str]) -> str:
        nonlocal count
        value = MaterialValue(int(match.group("index")), parse_vector(match.group("params")), parse_vector(match.group("colour")))  # type: ignore[arg-type]
        count += 1
        return f'{match.group("indent")}{match.group("field")}: {quoted(ids[value])}'

    rewritten = pattern.sub(replacement, text)
    if re.search(r"\b(?:materialIndex|materialDef):", rewritten):
        raise ValueError(f"{path}: legacy material data remained after rewrite")
    path.write_bytes(rewritten.encode("utf-8"))
    return count


def remove_materials_section(path: pathlib.Path) -> bool:
    lines = path.read_bytes().decode("utf-8").splitlines(keepends=True)
    try:
        start = next(i for i, line in enumerate(lines) if re.fullmatch(r"\s{2}Materials:\s*(?:\r?\n)?", line))
    except StopIteration:
        return False
    end = start + 1
    while end < len(lines):
        line = lines[end]
        if line.strip() and not line.lstrip().startswith("#") and len(line) - len(line.lstrip()) <= 2:
            break
        end += 1
    # The explanatory comment immediately above Materials belongs to the
    # retired section; remove its contiguous two-space-indented block too.
    comment_start = start
    while comment_start > 0 and re.fullmatch(r"\s{2}#.*(?:\r?\n)?", lines[comment_start - 1]):
        comment_start -= 1
    path.write_bytes("".join(lines[:comment_start] + lines[end:]).encode("utf-8"))
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=pathlib.Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--override-config", type=pathlib.Path, default=DEFAULT_CONFIGS[0],
                        help="Game.yaml whose Materials values are applied before cleanup")
    parser.add_argument("--level", type=pathlib.Path, action="append", default=None,
                        help="legacy level to rewrite (repeatable; defaults to world-test-1.yaml)")
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--clean-config", type=pathlib.Path, action="append", default=None,
                        help="Game.yaml to remove Materials from (repeatable; defaults to all three configs)")
    args = parser.parse_args(argv)
    levels = args.level or [DEFAULT_LEVEL]
    configs = args.clean_config or DEFAULT_CONFIGS

    techniques = parse_registry(args.registry)
    overrides = parse_game_overrides(args.override_config)
    for config in configs:
        candidate = parse_game_overrides(config)
        if candidate != overrides:
            raise ValueError(
                f"{config} has different Materials overrides from {args.override_config}; "
                "one built-in catalog cannot preserve configuration-specific values")
    apply_overrides(techniques, overrides)
    migrated = collect_level_materials(levels, techniques)
    ids = migrated_ids(migrated, techniques)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(render_catalog(techniques, migrated, ids), encoding="utf-8", newline="\n")
    rewritten = sum(rewrite_level(path, ids) for path in levels)
    cleaned = sum(remove_materials_section(path) for path in configs)
    print(f"Wrote {args.output} with {len(techniques)} built-in and {len(migrated)} migrated Sub-material(s)")
    print(f"Rewrote {rewritten} level material reference(s); cleaned {cleaned} Game.yaml file(s)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"migration failed: {error}", file=sys.stderr)
        raise SystemExit(1)
