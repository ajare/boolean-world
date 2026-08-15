#!/usr/bin/env python3
"""Convert a Willpower resource-definition XML file to the YAML the new
resource system reads.

The mapping is fixed by how utils::YamlReader builds StructuredData and how
wp::DataNode reads it back (see DataNode.cpp):

  element                -> key
  attribute              -> child key with a scalar value
  text, element is empty -> scalar value for the key
  text + attributes      -> an extra "value" key, because DataNode::getValue()
                            falls back to the "value" entry when the node is
                            not itself a scalar
  repeated siblings      -> a YAML sequence under the shared key, which
                            YamlReader unpacks back into repeated entries

Usage:  python tools/xml_to_resource_yaml.py <in.xml> <out.yaml>
"""
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict

INDENT = "  "


def quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def node_to_obj(el):
    """Return either a string (pure scalar) or an OrderedDict of key -> value,
    where a repeated key maps to a list."""
    text = (el.text or "").strip()
    has_children = len(el) > 0
    has_attrs = len(el.attrib) > 0

    if not has_children and not has_attrs:
        return text  # pure scalar (possibly empty)

    out = OrderedDict()
    for k, v in el.attrib.items():
        out[k] = v
    if text:
        # Mixed content: DataNode::getValue() looks for a "value" entry.
        out["value"] = text

    for child in el:
        obj = node_to_obj(child)
        if child.tag in out:
            if not isinstance(out[child.tag], list):
                out[child.tag] = [out[child.tag]]
            out[child.tag].append(obj)
        else:
            out[child.tag] = obj
    return out


def emit(obj, depth, lines):
    pad = INDENT * depth
    if isinstance(obj, str):
        raise AssertionError("scalars are emitted by the caller")
    for key, val in obj.items():
        if isinstance(val, list):
            lines.append("%s%s:" % (pad, key))
            for item in val:
                if isinstance(item, str):
                    lines.append("%s- %s" % (INDENT * (depth + 1), quote(item)))
                else:
                    inner = []
                    emit(item, 0, inner)
                    if not inner:
                        lines.append("%s- {}" % (INDENT * (depth + 1)))
                        continue
                    lines.append("%s- %s" % (INDENT * (depth + 1), inner[0].lstrip()))
                    for extra in inner[1:]:
                        lines.append(INDENT * (depth + 2) + extra)
        elif isinstance(val, str):
            lines.append("%s%s: %s" % (pad, key, quote(val)))
        else:
            if not val:
                lines.append("%s%s: {}" % (pad, key))
            else:
                lines.append("%s%s:" % (pad, key))
                emit(val, depth + 1, lines)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    root = ET.parse(src).getroot()
    obj = node_to_obj(root)

    lines = []
    if isinstance(obj, str):
        lines.append("%s: %s" % (root.tag, quote(obj)))
    elif not obj:
        lines.append("%s: {}" % root.tag)
    else:
        lines.append("%s:" % root.tag)
        emit(obj, 1, lines)

    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d lines)" % (dst, len(lines)))


main()
