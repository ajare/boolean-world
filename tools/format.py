#!/usr/bin/env python3
"""Run clang-format over BooleanWorld's own C++ sources.

Vendored third-party code is excluded by explicit path so it stays
byte-comparable against upstream. The list is deliberately explicit rather
than a name pattern: several of our own files have names that look vendored
(Launcher/ImGuiDataProvider.h and Launcher/glfw/ImGuiGLFW.*, for example), and
a prefix rule silently skips them.

Usage:
    python tools/format.py [--check] [library ...]

    library   Willpower | AppLib | Launcher | BooleanWorld   (default: all)
    --check   report files that would change; do not rewrite
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CLANG_FORMAT = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"

LIBRARIES = {
    "Willpower": [os.path.join("src", "Willpower")],
    "AppLib": [os.path.join("src", "AppLib")],
    "Launcher": ["Launcher"],
    "BooleanWorld": [os.path.join("Applications", "BooleanWorld")],
}

# Directories that contain nothing but vendored code.
VENDOR_DIRS = [
    os.path.join("Applications", "BooleanWorld", "app", "include", "imgui"),
    os.path.join("Applications", "BooleanWorld", "app", "src", "imgui"),
    os.path.join("Launcher", "include", "imgui"),
    os.path.join("Launcher", "src", "imgui"),
]

# Vendored files that sit alongside our own sources in the same directory.
# editor/ carries a full ImGui + ImPlot + addon set inline.
_VENDOR_BASENAMES = {
    "imgui.h", "imgui.cpp", "imgui_internal.h", "imconfig.h",
    "imgui_demo.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp",
    "imgui_impl_opengl3.h", "imgui_impl_opengl3.cpp", "imgui_impl_opengl3_loader.h",
    "imgui_impl_sdl2.h", "imgui_impl_sdl2.cpp",
    "imgui_impl_sdl3.h", "imgui_impl_sdl3.cpp",
    "imgui-knobs.h", "imgui-knobs.cpp",
    "imgui_curve.hpp", "imgui_markdown.h",
    "imnodes.h", "imnodes.cpp", "imnodes_internal.h",
    "implot.h", "implot.cpp", "implot_internal.h",
    "implot_demo.cpp", "implot_items.cpp",
    "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h",
}

# Individually vendored files elsewhere in the tree.
VENDOR_FILES = [
    # Jochen Kalmbach's StackWalker.
    os.path.join("src", "Willpower", "willpower.common", "include", "willpower", "common", "StackWalker.h"),
    os.path.join("src", "Willpower", "willpower.common", "src", "StackWalker.cpp"),
    # Angus Johnson's independent Clipper 1 implementation.
    os.path.join("src", "Willpower", "willpower.geometry", "include", "willpower", "geometry", "clipper.hpp"),
    os.path.join("src", "Willpower", "willpower.geometry", "src", "clipper.cpp"),
]

SKIP_DIRS = {"obj", "bin", "lib", ".vs", "build-cmake", "ext", "vendor", "__pycache__", ".git"}
EXTENSIONS = (".h", ".hpp", ".cpp", ".inl")


def is_vendored(rel):
    if rel in VENDOR_FILES:
        return True
    for d in VENDOR_DIRS:
        if rel.startswith(d + os.sep):
            return True
    return os.path.basename(rel) in _VENDOR_BASENAMES


def collect(library):
    out = []
    for root in LIBRARIES[library]:
        base = os.path.join(REPO, root)
        for dp, dn, fn in os.walk(base):
            dn[:] = [d for d in dn if d not in SKIP_DIRS]
            for f in fn:
                if not f.endswith(EXTENSIONS):
                    continue
                ap = os.path.join(dp, f)
                rel = os.path.relpath(ap, REPO)
                if is_vendored(rel):
                    continue
                out.append(rel)
    return sorted(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    libs = args or list(LIBRARIES)

    if not os.path.isfile(CLANG_FORMAT):
        sys.exit("clang-format not found at %s" % CLANG_FORMAT)

    grand = 0
    for lib in libs:
        files = collect(lib)
        grand += len(files)
        if check:
            changed = []
            for rel in files:
                r = subprocess.run([CLANG_FORMAT, "--dry-run", "--Werror", rel],
                                   cwd=REPO, capture_output=True)
                if r.returncode != 0:
                    changed.append(rel)
            print("%-14s %4d files, %4d would change" % (lib, len(files), len(changed)))
        else:
            # clang-format takes many paths at once; chunk to stay under the
            # Windows command-line limit.
            for i in range(0, len(files), 40):
                subprocess.run([CLANG_FORMAT, "-i"] + files[i:i + 40],
                               cwd=REPO, check=True)
            print("%-14s %4d files formatted" % (lib, len(files)))
    print("%-14s %4d files total" % ("", grand))


main()
