#pragma once

#include <willpower/common/Vector2.h>

#include <core/WorldData.h>

#include "imgui.h"

#include "Defines.h"
#include "Document.h"
#include "Settings.h"

namespace editor {
struct ScrollingBuffer {
  int MaxSize;
  int Offset;
  float CurMin, CurMax;
  ImVector<ImVec2> Data;

  ScrollingBuffer(int max_size = 2000) {
    MaxSize = max_size;
    Offset = 0;
    CurMin = 0;
    CurMax = 0;
    Data.reserve(MaxSize);
  }

  void AddPoint(float x, float y) {
    if (Data.size() < MaxSize) {
      Data.push_back(ImVec2(x, y));
    } else {
      Data[Offset] = ImVec2(x, y);
      Offset = (Offset + 1) % MaxSize;
    }

    if (y > CurMax) {
      CurMax = y;
    }
    if (y < CurMin) {
      CurMin = y;
    }
  }

  void Erase() {
    if (Data.size() > 0) {
      Data.shrink(0);
      Offset = 0;
    }

    CurMin = 0;
    CurMax = 0;
  }
};

bool mouseInteractingWithBackground();

wp::Vector2 getMouseWorldPosition();

// The inverse of Render.cpp's worldToScreen() for an arbitrary screen point,
// rather than just the current mouse position - used to convert a rubber-band
// selection rectangle's corners once the drag that defined them has moved the
// mouse on.
wp::Vector2 screenToWorldPosition(ImVec2 const& screenPos);

uint32_t getHoveredPrimitiveIndex(editor::Document* doc, Settings const& settings);

std::vector<uint32_t> getHoveredPrimitiveIndices(editor::Document* doc, Settings const& settings);

uint32_t getHoveredWorldVertexIndex(editor::Document* doc, Settings const& settings, bw::core::WorldData const* worldData);

uint32_t getHoveredTriggerLineIndex(editor::Document* doc, Settings const& settings);

void generateClipping(editor::Document* doc, Settings const& settings, int flag);

// Rebuilds the World's arrangement and the render data derived from it. Unlike
// generateClipping this is not gated on configFlags: an explicit request - the
// menu item, the toolbar button, the P shortcut - is not something an
// auto-clipping preference should be able to swallow.
void regenerateWorldData(editor::Document* doc);

// Installs primitiveVisibleForActiveStep (declared in Document.h - it also
// bounds Document's own selection queries, which have no business depending
// on this file's ImGui-linked helpers) on the Document, which applies it to
// every World it builds from here on, and regenerates the current one. The
// filter reads settings live, so it must outlive the Document - pass the
// editor's global Settings, not a temporary.
void applyStepVisibilityFilter(editor::Document* doc, Settings const& settings);

}  // namespace editor
