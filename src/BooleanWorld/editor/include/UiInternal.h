#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <nfd/nfd.h>

#include <core/DefinePrefabs.h>
#include <core/LayerBuildStep.h>
#include <core/TileMap.h>
#include <core/WorldData.h>
#include <core-lua/RunScript.h>
#include <core/RegularPolygon.h>
#include <core/CirclePolygon.h>
#include <core/CircleSegmentPolygon.h>
#include <core/TorusPolygon.h>
#include <core/TorusSegmentPolygon.h>
#include <core/RectanglePolygon.h>
#include <core/SuperformulaPolygon.h>
#include <core/MeshPrimitive.h>
#include <core/PrimitiveFactory.h>
#include <core/Defines.h>
#include <core/DynamicWorldDataGenerator.h>
#include <core/LiquidType.h>
#include <common/MaterialRegistry.h>
#include <willpower/application/resourcesystem/ImageResource.h>
#include <willpower/application/resourcesystem/ResourceManager.h>

#include <GL/glew.h>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "IconsFontAwesome5.h"

#include "UI.h"
#include "UiHelpers.h"
#include "WidgetHelpers.h"
#include "AppHelpers.h"
#include "Defines.h"
#include "Document.h"
#include "Undo.h"
#include "Actions.h"
#include "Markdown.h"
#include "PrefabTilingGuide.h"
#include "PrimitiveFieldPreview.h"
#include "PrimitiveFieldPlacement.h"
#include "Preview3D.h"
#include "ProcMaterialLibrary.h"
#include "SubMaterialThumbnailRenderer.h"
#include "ExitApplicationException.h"
#include "EditorRenderSystem.h"
#include "EmbossingCatalogLibrary.h"
#include "Render.h"
#include "HoverableType.h"

extern std::map<std::string, std::string> gHelpFiles;
extern wp::Vector2 gViewOffset;
extern float gViewZoom;
extern wp::Vector2 gWorldViewScreenOrigin;
extern wp::Vector2 gWorldViewSize;
extern editor::HoverableType gHoveredType;
extern std::vector<uint32_t> gHoveredIndices;
extern editor::EditorInteraction gEditorInteraction;

namespace editor {

using CreatePrimitiveFunction =
    std::function<std::unique_ptr<bw::core::Primitive>()>;

CreatePrimitiveFunction createPrimitiveFunction(bw::core::PrimitiveSpec spec);
std::filesystem::path editorResourceRoot();
void cycleGridSize(Settings& settings);
void resetAnimatorCaptures(Document* doc);

bw::core::Primitive::Operation setOperationWidget(
    Document* doc, bw::core::Primitive* primitive, int mode);
bw::core::Primitive::FillRule setFillRuleWidget(
    Document* doc, bw::core::Primitive* primitive, int mode);
void renderAnimatedProperty(
    Document* doc, bw::core::Primitive* primitive,
    bw::core::VertexTransformer::Key key, Settings& settings,
    double globalTime);
std::string worldResourceReference(
    wp::application::resourcesystem::Resource const& resource);
int wallMaskChannelCount(char const* reference);
uint32_t wallMaskBlendedPreviewTexture(
    uint32_t standardTexture, uint32_t secondaryTexture,
    std::string const& maskReference, int maskChannel,
    std::vector<float> const& secondaryParams,
    std::array<float, 3> const& secondaryColour);
extern std::unique_ptr<SubMaterialThumbnailRenderer>
    gMaterialPickerThumbnails;

void renderMenu(ViewContext& context);
void renderToolbar(ViewContext& context);
void renderStatusbar(ViewContext& context);
void renderWorldView(ViewContext& context);
void renderBuildVariablesEditor(
    ViewContext& context, bw::core::Layer* layer = nullptr);
void renderCreatePrimitiveView(ViewContext& context);
void renderEditPrimitiveView(ViewContext& context);
bool hasEditablePrimitiveSelection(Document* doc);
void renderPrimitiveOrderView(ViewContext& context);
void renderLayerStepsView(ViewContext& context);
void renderPrefabsView(ViewContext& context,
                       bw::core::DefinePrefabs* definePrefabs);
void renderSelectedPrefabView(ViewContext& context,
                              bw::core::DefinePrefabs* definePrefabs,
                              bw::core::Prefab* prefab);
void renderPrefabFieldView(ViewContext& context,
                           bw::core::PrefabField* prefabField);
void renderRunScriptView(ViewContext& context, bw::core::RunScript* runScript);
void renderTileMapView(ViewContext& context, bw::core::TileMap* tileMap);
void renderCreateTriggerLineView(ViewContext& context);
void renderEditTriggerLineView(ViewContext& context,
                               uint32_t triggerLineIndex);
void renderArrangementFaceView(ViewContext& context);
void renderConfigView(ViewContext& context);
void renderHistoryView(ViewContext& context);
void renderMeshView(ViewContext& context);
void renderCombinedPanel(ViewContext& context);
void handleShortcuts(ViewContext& context);
void handleMouseInteraction(ViewContext& context);
void checkModalPopups(ViewContext& context);
void renderDebug(ViewContext& context);
void renderContextSensitiveHelp(ViewContext& context);
void renderPreviewDropControl(ViewContext& context);
void renderMainWindow(ImGuiID dockspaceId);
void renderEditor(ViewContext& context);

bool renderPrimitivePropertySet(
    bw::core::PrimitivePropertySet* properties, bool editable,
    Document* doc, Settings& settings,
    bw::core::Primitive* primitive = nullptr);
bool renderImageResourcePicker(char const* label, char* reference,
                               size_t referenceCapacity);

}  // namespace editor
