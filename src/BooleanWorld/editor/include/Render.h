#pragma once

#include <cstdint>
#include <set>

#include <core/ArrangementWorldData.h>
#include <core/WorldData.h>

#include "Document.h"
#include "Settings.h"

#define MINIMAP_SCALE (512.0f / 24.0f)
#define MINIMAP_PADDING 20.0f

void renderWorld(
    editor::Document* doc,
    editor::Settings const& settings,
    bw::core::ArrangementWorldData const* worldData,
    double globalTime);

void renderMiniMap(editor::Document* doc, editor::Settings const& settings, bw::core::WorldData const* worldData, double globalTime);

wp::BoundingBox getMiniMapBounds(editor::Document* doc);