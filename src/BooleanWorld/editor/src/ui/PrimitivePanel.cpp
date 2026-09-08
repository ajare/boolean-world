#define NOMINMAX

#include "UiInternal.h"

namespace editor {
using namespace std;

tuple<string, CreatePrimitiveFunction, bool> renderCreateRegularPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static int numSides = 3;
  bool modified{false};

  widgets::HelpMarker("Set the number of sides - minimum 3, maximum 8.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputInt("Sides##CreateRegularPolygon", &numSides, 1, 1)) {
    numSides = clamp(numSides, ED_MIN_REGULAR_POLYGON_SIDES, ED_MAX_REGULAR_POLYGON_SIDES);
    modified = true;
  }

  return {
      format("Create Regular {}-Gon Primitive", numSides),
      createPrimitiveFunction({bw::core::RegularPolygonSpec{static_cast<uint32_t>(numSides)}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateCirclePolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##CreateCircle", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Circle Primitive",
      createPrimitiveFunction({bw::core::CircleSpec{resolution}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateCircleSegmentPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float arcLength = 90.0f;
  static float resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##CreateCircleSegment", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);
    modified = true;
  }

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This value determines the number of sides in the circle segment polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##CreateCircleSegment", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Circle Segment Primitive",
      createPrimitiveFunction({bw::core::CircleSegmentSpec{arcLength, resolution}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateTorusPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float thickness = 0.5f, resolution = 0.5f;
  bool modified{false};

  ImGui::SetNextItemWidth(128);

  widgets::HelpMarker("This is the thickness of the torus, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##CreateTorus", &thickness, 0.01f, 0.99f)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("This value determines the number of sides in the torus polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateTorus", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Torus Primitive",
      createPrimitiveFunction({bw::core::TorusSpec{thickness, resolution}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateTorusSegmentPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float thickness = 0.5f;
  static float arcLength = 90.0f;
  static float resolution = 0.5f;
  bool modified{false};

  widgets::HelpMarker("This is the thickness of the torus segment, in [0.01, 0.99].");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Thickness##CreateTorusSegment", &thickness, 0.01f, 0.99f)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("ArcLength##CreateTorusSegment", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);
    modified = true;
  }

  widgets::HelpMarker("This value determines the number of sides in the torus segment polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateTorusSegment", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);
    modified = true;
  }

  return {
      "Create Torus Segment Primitive",
      createPrimitiveFunction({bw::core::TorusSegmentSpec{thickness, arcLength, resolution}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateRectanglePolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float xyRatio = 2.0f;
  bool modified{false};

  widgets::HelpMarker("This value sets the ratio of the rectangle width to its height.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Ratio##CreateRectangle", &xyRatio, 0.01f, 0.1f)) {
    xyRatio = clamp(xyRatio, ED_MIN_RECTANGLE_XYRATIO, ED_MAX_RECTANGLE_XYRATIO);
    modified = true;
  }

  return {
      "Create Rectangle Primitive",
      createPrimitiveFunction({bw::core::RectangleSpec{xyRatio}, op, fillRule, priority, position, scale, angle}),
      modified};
}

tuple<string, CreatePrimitiveFunction, bool> renderCreateSuperformulaPolygon(editor::Document* doc, bw::core::Primitive::Operation op, bw::core::Primitive::FillRule fillRule, uint8_t priority, wp::Vector2 const& position, float scale, float angle) {
  static float resolution = 0.5f;
  static float values[6] = {1.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  bool modified{false};

  widgets::HelpMarker("This value determines the number of sides in the superformula polygon.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Res##CreateSuperformula", &resolution, 0.01f, 0.1f)) {
    resolution = clamp(resolution, ED_MIN_SUPERFORMULA_RESOLUTION, 1.0f);
    modified = true;
  }

  widgets::HelpMarker("Preset for initial values.");
  ImGui::SameLine();
  static int selectedPreset{0};
  if (ImGui::Combo("Preset", &selectedPreset, "Soft triangle\0Soft square\0Starfish\0Soft X\0Eye\0Cusp\0\0", 6)) {
    modified = true;
    switch (selectedPreset) {
      case 0:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 3.0f;
        values[3] = 4.5f;
        values[4] = 10.0f;
        values[5] = 10.0f;
        break;

      case 1:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 4.0f;
        values[3] = 12.0f;
        values[4] = 15.0f;
        values[5] = 15.0f;
        break;

      case 2:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 5.0f;
        values[3] = 2.0f;
        values[4] = 7.0f;
        values[5] = 7.0f;
        break;

      case 3:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 4.0f;
        values[3] = 1.0f;
        values[4] = 7.0f;
        values[5] = 8.0f;
        break;

      case 4:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 2.0f;
        values[3] = 0.5f;
        values[4] = 0.5f;
        values[5] = 0.5f;
        break;

      case 5:
        values[0] = 1.0f;
        values[1] = 1.0f;
        values[2] = 2.0f;
        values[3] = 1.0f;
        values[4] = 1.0f;
        values[5] = 1.0f;
        break;
      default:
        throw EditorException("Unknown preset");
    }
  }

  widgets::HelpMarker("Superformula parameter.  This is usually set to 1.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("a##CreateSuperformula", &values[0], ED_MIN_SUPERFORMULA_A, ED_MAX_SUPERFORMULA_A)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Superformula parameter.  This is usually set to 1.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("b##CreateSuperformula", &values[1], ED_MIN_SUPERFORMULA_B, ED_MAX_SUPERFORMULA_B)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("m##CreateSuperformula", &values[2], ED_MIN_SUPERFORMULA_M, ED_MAX_SUPERFORMULA_M)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n1##CreateSuperformula", &values[3], ED_MIN_SUPERFORMULA_N1, ED_MAX_SUPERFORMULA_N1)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n2##CreateSuperformula", &values[4], ED_MIN_SUPERFORMULA_N2, ED_MAX_SUPERFORMULA_N2)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  widgets::HelpMarker("Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.");
  ImGui::SameLine();

  ImGui::SetNextItemWidth(128);
  if (ImGui::SliderFloat("n3##CreateSuperformula", &values[5], ED_MIN_SUPERFORMULA_N3, ED_MAX_SUPERFORMULA_N3)) {
    modified = true;
  }

  if (ImGui::IsItemDeactivatedAfterEdit()) {
    regenerateWorldData(doc);
  }

  return {
      "Create Superformula Primitive",
      createPrimitiveFunction({bw::core::SuperformulaSpec{{values[0], values[1], values[2], values[3], values[4], values[5]}, resolution}, op, fillRule, priority, position, scale, angle}),
      modified};
}

bw::core::Primitive::Operation setOperationWidget(Document* doc, bw::core::Primitive* primitive, int mode) {
  int selectedOperation;
  bw::core::Primitive::Operation editOperation = primitive ? primitive->getOperation()
                                                           : bw::core::Primitive::Operation::Union;

  string name;
  switch (mode) {
    case 0:
      name = "Operation##CreatePrimitive";
      break;

    case 1:
      name = "Operation##EditPrimitive";
      break;

    case 2:
      name = "Operation##OrderPrimitive";
      break;

    case 3:
      name = "Operation##MeshDraw";
      break;

    default:
      throw EditorException("Unknown widget mode");
  }
  switch (editOperation) {
    case bw::core::Primitive::Operation::Union:
      selectedOperation = 0;
      break;

    case bw::core::Primitive::Operation::Difference:
      selectedOperation = 1;
      break;

    case bw::core::Primitive::Operation::Intersection:
      selectedOperation = 2;
      break;

    case bw::core::Primitive::Operation::XOR:
      selectedOperation = 3;
      break;

    default:
      throw EditorException("Unknown operation");
  }

  widgets::HelpMarker("CSG operation to perform on primitives.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo(name.c_str(), &selectedOperation, "Union\0Difference\0Intersection\0XOR\0\0", 6)) {
    switch (selectedOperation) {
      case 0:
        editOperation = bw::core::Primitive::Operation::Union;
        break;

      case 1:
        editOperation = bw::core::Primitive::Operation::Difference;
        break;

      case 2:
        editOperation = bw::core::Primitive::Operation::Intersection;
        break;

      case 3:
        editOperation = bw::core::Primitive::Operation::XOR;
        break;

      default:
        throw EditorException("Unknown operation");
    }

    if (primitive) {
      transact(doc, CommandId::SetPrimitiveOperation, [&] {
        setPrimitiveOperation(doc, primitive, editOperation);
      });
    }
  }

  return editOperation;
}

bw::core::Primitive::FillRule setFillRuleWidget(Document* doc, bw::core::Primitive* primitive, int mode) {
  int selectedFillRule;
  bw::core::Primitive::FillRule editFillRule = primitive ? primitive->getFillRule()
                                                         : bw::core::Primitive::FillRule::NonZero;

  string name;
  switch (mode) {
    case 0:
      name = "FillRule##CreatePrimitive";
      break;

    case 1:
      name = "FillRule##EditPrimitive";
      break;

    case 2:
      name = "FillRule##OrderPrimitive";
      break;

    case 3:
      name = "FillRule##MeshDraw";
      break;

    default:
      throw EditorException("Unknown widget mode");
  }

  switch (editFillRule) {
    case bw::core::Primitive::FillRule::NonZero:
      selectedFillRule = 0;
      break;

    case bw::core::Primitive::FillRule::EvenOdd:
      selectedFillRule = 1;
      break;

    default:
      throw EditorException("Unknown fill rule");
  }

  widgets::HelpMarker("This determines whether intersecting sections are filled using non-zero winding or even-odd parity.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo(name.c_str(), &selectedFillRule, "NonZero\0EvenOdd\0\0", 6)) {
    switch (selectedFillRule) {
      case 0:
        editFillRule = bw::core::Primitive::FillRule::NonZero;
        break;

      case 1:
        editFillRule = bw::core::Primitive::FillRule::EvenOdd;
        break;

      default:
        throw EditorException("Unknown fill rule");
    }

    if (primitive) {
      transact(doc, CommandId::SetPrimitiveFillRule, [&] {
        setPrimitiveFillRule(doc, primitive, editFillRule);
      });
    }
  }

  return editFillRule;
}

void renderCreateNewPrimitive(editor::Document* doc, editor::Settings& settings) {
  static int selectedPrimitiveType = 0;
  bool modified{false};
  bool committed{false};

  auto ghost = doc->getWorld()->getPrimitive(0);

  bw::core::Primitive::Operation createOperation = ghost->getOperation();
  bw::core::Primitive::FillRule createFillRule = ghost->getFillRule();

  // Primitive tyoe
  vector<string> primitiveTypes = {
      "Regular Polygon",
      "Circle",
      "Circle Segment",
      "Torus",
      "Torus Segment",
      "Rectangle",
      "Superformula",
  };

  string primitiveTypesStr;

  for (auto const& primitiveType : primitiveTypes) {
    primitiveTypesStr += primitiveType;
    primitiveTypesStr += '\0';
  }

  widgets::HelpMarker("Choose the primitive type to create.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::Combo("Type##CreatePrimitive", &selectedPrimitiveType, primitiveTypesStr.c_str(), 6)) {
    modified = true;
  }

  // Operation
  createOperation = setOperationWidget(doc, ghost, 0);

  // Fill rule
  createFillRule = setFillRuleWidget(doc, ghost, 0);

  // Priority
  int primitivePriority = (int)ghost->getPriority();
  widgets::HelpMarker(
      "Priority determines fold order within this LayerBuildStep. Layer and step order take precedence.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderInt("Priority##CreatePrimitive", &primitivePriority, BW_PRIORITY_MIN_VALUE, BW_PRIORITY_MAX_VALUE)) {
    modified = true;
  }

  committed |= ImGui::IsItemDeactivatedAfterEdit();

  // Position
  auto const& pos = ghost->getPosition();
  float primitivePosition[2] = {pos.x, pos.y};

  widgets::HelpMarker("Initial position in the world.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat2("Position##CreatePrimitive", primitivePosition)) {
    modified = true;
  }

  wp::Vector2 primitivePos{primitivePosition[0], primitivePosition[1]};

  // Scale
  float primitiveScale = ghost->getSize().x;
  widgets::HelpMarker("Initial size.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Size##CreatePrimitive", &primitiveScale, ED_MIN_PRIMITIVE_SIZE, ED_MAX_PRIMITIVE_SIZE)) {
    modified = true;
  }

  committed |= ImGui::IsItemDeactivatedAfterEdit();

  // Angle
  float primitiveAngle = ghost->getAnimationInterpolator(bw::core::VertexTransformer::Key::Angle).getValue(0.0f);
  widgets::HelpMarker("Initial angle.  This will create two keyframes in the angle interpolator at times 0 and 1, set to this value.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Angle##CreatePrimitive", &primitiveAngle, 0.0f, 360.0f)) {
    modified = true;
  }

  committed |= ImGui::IsItemDeactivatedAfterEdit();

  tuple<string, CreatePrimitiveFunction, bool> funcDetails;
  switch (selectedPrimitiveType) {
    case 0:
      funcDetails = renderCreateRegularPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 1:
      funcDetails = renderCreateCirclePolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 2:
      funcDetails = renderCreateCircleSegmentPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 3:
      funcDetails = renderCreateTorusPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 4:
      funcDetails = renderCreateTorusSegmentPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 5:
      funcDetails = renderCreateRectanglePolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    case 6:
      funcDetails = renderCreateSuperformulaPolygon(doc, createOperation, createFillRule, (uint8_t)primitivePriority, primitivePos, primitiveScale, primitiveAngle);
      break;

    default:
      throw EditorException("Unknown primitive type");
  }

  auto funcText = get<0>(funcDetails);
  auto newPrimitiveFunc = get<1>(funcDetails);
  auto primOptionsModified = get<2>(funcDetails);

  if (modified || primOptionsModified) {
    auto newGhost = newPrimitiveFunc();
    doc->updateGhost(doc->getWorld(), newGhost.release());
  }

  if (committed) {
    regenerateWorldData(doc);
  }

  auto* activeStep = doc->getWorld()->getActiveLayer()->getActiveStep();
  auto acceptsNewPrimitives = activeStep->acceptsNewPrimitives();
  auto* definePrefabs = dynamic_cast<bw::core::DefinePrefabs*>(activeStep);
  auto unavailableReason = definePrefabs && !definePrefabs->getSelectedPrefab()
                               ? "Select a Prefab first."
                               : "The selected step does not accept new Primitives.";
  widgets::HelpMarker(acceptsNewPrimitives
                          ? "Create a Primitive in the active Layer's selected step."
                          : unavailableReason);
  ImGui::SameLine();
  ImGui::BeginDisabled(!acceptsNewPrimitives);
  if (ImGui::Button("Create##CreatePrimitive")) {
    transact(doc, CommandId::CreatePrimitiveFromGhost, [&] { createPrimitiveFromGhost(doc); });
  }
  ImGui::EndDisabled();
}

void renderCreatePrimitiveView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto windowFlags = 0;

  bool docIsActive = doc->isActive();

  if (!docIsActive) {
    widgets::PushDisabled();
  }

  renderCreateNewPrimitive(doc, settings);

  if (!docIsActive) {
    widgets::PopDisabled();
  }
}

void renderEditRegularPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
}

void renderEditCirclePolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto circle = static_cast<bw::core::CirclePolygon*>(primitive);
  float resolution = circle->getResolution();

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      auto staticBefore = circle->isStatic();

      circle->setResolution(resolution);
    });
  }
}

void renderEditCircleSegmentPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto circleSeg = static_cast<bw::core::CircleSegmentPolygon*>(primitive);
  float arcLength = circleSeg->getArcLength();
  float resolution = circleSeg->getResolution();

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##EditPrimitive", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      auto staticBefore = circleSeg->isStatic();

      circleSeg->setArcLength(arcLength);
    });
  }

  widgets::HelpMarker("This value determines the number of sides in the circle polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      circleSeg->setResolution(resolution);
    });
  }
}

void renderEditTorusPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto torus = static_cast<bw::core::TorusPolygon*>(primitive);
  float thickness = torus->getThickness();
  float resolution = torus->getResolution();

  widgets::HelpMarker("This is the thickness of the torus, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##EditTorus", &thickness, 0.01f, 0.99f)) {
    torus->setThickness(thickness);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Torus Thickness to {}", torus->getThickness()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  widgets::HelpMarker("This value determines the number of sides in the torus polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      torus->setResolution(resolution);
    });
  }
}

void renderEditTorusSegmentPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto torusSeg = static_cast<bw::core::TorusSegmentPolygon*>(primitive);
  float thickness = torusSeg->getThickness();
  float arcLength = torusSeg->getArcLength();
  float resolution = torusSeg->getResolution();

  widgets::HelpMarker("This is the thickness of the torus segment, in [0.01, 0.99].");
  ImGui::SameLine();
  if (ImGui::SliderFloat("Thickness##EditTorusSegment", &thickness, 0.01f, 0.99f)) {
    torusSeg->setThickness(thickness);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Torus Segment Thickness to {}", torusSeg->getThickness()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  widgets::HelpMarker("Arc length in degrees.");
  ImGui::SameLine();
  if (ImGui::InputFloat("ArcLength##EditPrimitive", &arcLength, 1.0f, 5.0f)) {
    arcLength = clamp(arcLength, ED_MIN_ARC_LENGTH, ED_MAX_ARC_LENGTH);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      auto staticBefore = torusSeg->isStatic();

      torusSeg->setArcLength(arcLength);
    });
  }

  widgets::HelpMarker("This value determines the number of sides in the torus segment polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_CIRCLE_RESOLUTION, 1.0f);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      torusSeg->setResolution(resolution);
    });
  }
}

void renderEditRectanglePolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto rectangle = static_cast<bw::core::RectanglePolygon*>(primitive);

  float xyRatio = rectangle->getXyRatio();

  widgets::HelpMarker("This value sets the ratio of the rectangle width to its height.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Ratio##EditRectangle", &xyRatio, 0.01f, 0.1f)) {
    auto staticBefore = rectangle->isStatic();

    xyRatio = clamp(xyRatio, ED_MIN_RECTANGLE_XYRATIO, ED_MAX_RECTANGLE_XYRATIO);

    rectangle->setXyRatio(xyRatio);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Rectangle X/Y ratio to {}", rectangle->getXyRatio()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }
}

void completeSuperformulaControlValueEdit(editor::Document* doc, bw::core::SuperformulaPolygon* superformula, uint32_t index, string const& name) {
  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Superformula {} to {}", name, superformula->getValue(index)));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }
}

void renderEditSuperformulaPolygon(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  ImGui::SetNextItemWidth(128);

  auto sf = static_cast<bw::core::SuperformulaPolygon*>(primitive);
  float resolution = sf->getResolution();

  widgets::HelpMarker("This value determines the number of sides in the superformula polygon.");
  ImGui::SameLine();
  if (ImGui::InputFloat("Res##EditPrimitive", &resolution, 0.01f, 0.1f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    resolution = clamp(resolution, ED_MIN_SUPERFORMULA_RESOLUTION, 1.0f);

    transact(doc, CommandId::EditPrimitiveShape, [&] {
      sf->setResolution(resolution);
    });
  }

  float values[6];
  for (uint32_t i = 0; i < 6; ++i) {
    values[i] = sf->getValue(i);
  }

  struct SuperformulaControl {
    char const* label;
    char const* name;
    char const* help;
    float minimum;
    float maximum;
  };
  SuperformulaControl const controls[] = {
      {"a##EditPrimitive", "a", "Superformula parameter.  This is usually set to 1.", ED_MIN_SUPERFORMULA_A, ED_MAX_SUPERFORMULA_A},
      {"b##EditPrimitive", "b", "Superformula parameter.  This is usually set to 1.", ED_MIN_SUPERFORMULA_B, ED_MAX_SUPERFORMULA_B},
      {"m##EditPrimitive", "m", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_M, ED_MAX_SUPERFORMULA_M},
      {"n1##EditPrimitive", "n1", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N1, ED_MAX_SUPERFORMULA_N1},
      {"n2##EditPrimitive", "n2", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N2, ED_MAX_SUPERFORMULA_N2},
      {"n3##EditPrimitive", "n3", "Superformula parameter.  See https://en.wikipedia.org/wiki/Superformula for examples.", ED_MIN_SUPERFORMULA_N3, ED_MAX_SUPERFORMULA_N3},
  };

  for (uint32_t i = 0; i < 6; ++i) {
    auto const& control = controls[i];
    widgets::HelpMarker(control.help);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    if (ImGui::SliderFloat(control.label, &values[i], control.minimum, control.maximum)) {
      sf->setValue(i, values[i]);
    }
    completeSuperformulaControlValueEdit(doc, sf, i, control.name);
  }
}

bool renderEditMeshPrimitive(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  auto* mesh = static_cast<bw::core::MeshPrimitive*>(primitive);
  auto const filledRegionCount = mesh->flattenTree().size();
  ImGui::BeginDisabled(filledRegionCount <= 1);
  auto decompose = ImGui::Button("Decompose");
  ImGui::EndDisabled();
  if (decompose) {
    auto index = primitive->getId();
    transact(doc, CommandId::DecomposeMeshPrimitive, [&] { decomposeMeshPrimitive(doc, index); });
  }
  widgets::HelpMarker(
      "Replace this MeshPrimitive with one Union MeshPrimitive per filled "
      "region, retaining each region's direct Holes.");
  return decompose;
}

void renderEditPrimitiveGeometry(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings, double globalTime) {
  if (primitive->isStatic()) {
    ImGui::Text("Primitive is static");
  } else {
    ImGui::Text("Primitive is animated");
  }

  // Copy and rotate
  static float copyAngle{0.0f};

  ImGui::SetNextItemWidth(128);
  ImGui::SliderAngle("Copy Angle", &copyAngle, 0, 360);
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_CLONE)) {
    auto index = primitive->getId();
    transact(doc, CommandId::CloneRotatedPrimitive, [&] { cloneRotatedPrimitive(doc, index, wp::MathsUtils::degrees(copyAngle)); });
  }

  ImGui::Separator();

  setOperationWidget(doc, primitive, 1);
  if (!dynamic_cast<bw::core::MeshPrimitive*>(primitive)) {
    setFillRuleWidget(doc, primitive, 1);
  }

  // Priority
  int primitivePriority = (int)primitive->getPriority();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderInt(
          "Priority##EditPrimitive", &primitivePriority,
          BW_PRIORITY_MIN_VALUE, BW_PRIORITY_MAX_VALUE)) {
    primitive->setPriority((uint8_t)primitivePriority);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Primitive Priority to {}", (int)primitive->getPriority()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  float primitiveSize = primitive->getSize().x;

  widgets::HelpMarker("Set the base size of the primitive.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::SliderFloat("Size##EditPrimitive", &primitiveSize, ED_MIN_PRIMITIVE_SIZE, ED_MAX_PRIMITIVE_SIZE)) {
    primitive->setSize(primitiveSize, primitiveSize);

    // Update vertices for visual purposes
    primitive->updateVertexPositions();
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Primitive Size to {}", primitive->getSize().x));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  wp::Vector2 const& primitivePosition = primitive->getPosition();

  widgets::HelpMarker("Set the origin (global centre position) of the primitive.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pPosition[2] = {primitivePosition.x, primitivePosition.y};

  if (ImGui::InputFloat2("Position##EditPrimitive", pPosition)) {
    wp::Vector2 position{pPosition[0], pPosition[1]};

    transact(doc, CommandId::SetPrimitivePosition, [&] { setPrimitivePosition(doc, primitive, position); });
  }

  wp::Vector2 const& primitiveTransformOrigin = primitive->getTransformOffset();

  widgets::HelpMarker("Set the transform offset (ie scale/rotation centre) of the primitive.  This value is in [-1, 1] as it is relative to the primitive boundaries: the cenre cannot be outside the primitive: for that, use orbit distance.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pTransformOrigin[2] = {primitiveTransformOrigin.x, primitiveTransformOrigin.y};

  if (ImGui::InputFloat2("Transform offset##EditPrimitive", pTransformOrigin)) {
    // wp::Vector2 transformOrigin{
    //	clamp(pTransformOrigin[0], -1.0f, 1.0f),
    //	clamp(pTransformOrigin[1], -1.0f, 1.0f)
    // };
    wp::Vector2 transformOrigin = {pTransformOrigin[0], pTransformOrigin[1]};

    transact(doc, CommandId::SetPrimitiveTransformOffset, [&] { setPrimitiveTransformOffset(doc, primitive, transformOrigin); });
  }

  wp::Vector2 primitiveInfluenceOriginOffset = primitive->getInfluenceEyeOriginOffset();

  widgets::HelpMarker("Set the influence eye offset from the origin.  This determines the position from where input interpolators base their values.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  float pInfluenceOriginOffset[2] = {primitiveInfluenceOriginOffset.x, primitiveInfluenceOriginOffset.y};

  if (ImGui::InputFloat2("Influence Origin Offset##EditPrimitive", pInfluenceOriginOffset)) {
    wp::Vector2 influenceOriginOffset{pInfluenceOriginOffset[0], pInfluenceOriginOffset[1]};

    transact(doc, CommandId::SetPrimitiveInfluenceOriginOffset, [&] { setPrimitiveInfluenceOriginOffset(doc, primitive, influenceOriginOffset); });
  }

  if (primitive->getType() == "Regular") {
    renderEditRegularPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Circle") {
    renderEditCirclePolygon(doc, primitive, settings);
  } else if (primitive->getType() == "CircleSegment") {
    renderEditCircleSegmentPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Torus") {
    renderEditTorusPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "TorusSegment") {
    renderEditTorusSegmentPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Rectangle") {
    renderEditRectanglePolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Superformula") {
    renderEditSuperformulaPolygon(doc, primitive, settings);
  } else if (primitive->getType() == "Mesh") {
    if (renderEditMeshPrimitive(doc, primitive, settings)) {
      return;
    }
  } else {
    throw EditorException("Unknown primitive type: " + primitive->getType());
  }

  widgets::HelpMarker("If selected, this will orient a primitive so that it rotates to face the central point around which it is rotating, if an orbit distance of greater than zero is set.");
  ImGui::SameLine();
  bool orientOrbitAngle = primitive->getFollowOrbitAngle();
  if (ImGui::Checkbox("Follow orbit angle", &orientOrbitAngle)) {
    string action = orientOrbitAngle ? "Set Primitive angle to Orbit" : "Unset Primitive Angle to Orbit";

    // setPrimitiveFollowOrbitAngle already applies the value inside the
    // transaction, which then regenerates. Setting it again out here would
    // land after that regeneration snapshotted its input.
    transact(doc, CommandId::SetPrimitiveFollowOrbitAngle, [&] { setPrimitiveFollowOrbitAngle(doc, primitive, orientOrbitAngle); });
  }

  widgets::HelpMarker("Normally, angle from player to a primitive is taken with 0 degrees being [0, 1].  This value adds an offset (in degrees to that angle).");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  auto eyeAngleOffset = primitive->getInfluenceEyeAngleOffset();

  if (ImGui::SliderFloat("Eye Angle Offset##EditPrimitive", &eyeAngleOffset, 0.0f, 360.0f)) {
    primitive->setInfluenceEyeAngleOffset(eyeAngleOffset);
  }

  if (ImGui::IsItemActivated()) {
    beginTransaction(doc, CommandId::EditPrimitiveShape, 0.0f);
  } else if (ImGui::IsItemDeactivatedAfterEdit()) {
    commitUndoableAction(doc, format("Set Eye Angle Offset to {}", primitive->getInfluenceEyeAngleOffset()));
  } else if (ImGui::IsItemDeactivated()) {
    abandonUndoableAction(doc);
  }

  char const* animatedPropertyNames[] = {
      "Scale",
      "Angle",
      "Orbit Angle",
      "Orbit Distance"};

  for (int i = 0; i < (int)bw::core::VertexTransformer::Key::COUNT; ++i) {
    if (ImGui::CollapsingHeader(animatedPropertyNames[i])) {
      renderAnimatedProperty(doc, primitive, (bw::core::VertexTransformer::Key)i, settings, globalTime);
    }
  }
}

// The "Build step" combo in the Edit Primitive view. A Primitive may only be
// re-homed into a step of its own type, so a Layer whose steps are all
// different types offers nothing and the combo is shown disabled rather than
// hidden - which step a Primitive belongs to is worth reading even when it
// cannot be changed.
void renderPrimitiveBuildStep(editor::Document* doc, bw::core::Primitive* primitive) {
  auto* layer = doc->getWorld()->getActiveLayer();
  auto const sourceStepIndex = layer->getOwningStepIndex(primitive);

  if (sourceStepIndex == ~0u) {
    return;
  }

  auto const stepLabel = [layer](uint32_t index) {
    return format("{} :: {}", index, layer->getStep(index)->getType());
  };

  vector<uint32_t> targets;
  for (uint32_t i = 0; i < layer->getNumSteps(); ++i) {
    if (layer->canMovePrimitiveToStep(primitive, i)) {
      targets.push_back(i);
    }
  }

  widgets::HelpMarker(
      "The LayerBuildStep this Primitive is authored into. A Primitive can only move "
      "between steps of the same type, and only where both steps are enabled and the "
      "destination accepts new Primitives. Moving it changes where it folds: step order "
      "outranks Primitive priority.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220.0f);

  bool const movable = !targets.empty();
  if (!movable) {
    widgets::PushDisabled();
  }

  auto const currentLabel = stepLabel(sourceStepIndex);
  if (ImGui::BeginCombo("Build step", currentLabel.c_str())) {
    for (auto target : targets) {
      auto const label = stepLabel(target);
      if (ImGui::Selectable(label.c_str(), false)) {
        transact(doc, CommandId::MovePrimitiveToLayerBuildStep, [&] { movePrimitiveToLayerBuildStep(doc, layer, primitive, target); });
      }
    }
    ImGui::EndCombo();
  }

  if (!movable) {
    widgets::PopDisabled();
  }
}

void renderEditPrimitiveSettings(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  renderPrimitiveBuildStep(doc, primitive);

  int flags = (int)primitive->getFlags();

  auto f0 = ImGui::CheckboxFlags("Don't update Primitive Time when Player is static", &flags, BW_PRIMITIVE_NO_TIME_UPDATE_PLAYER_STATIC);
  auto f1 = ImGui::CheckboxFlags("Don't update Primitive Time when visible to Player", &flags, BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE);
  auto f2 = ImGui::CheckboxFlags("Calculate exact bounds based on vertex position", &flags, BW_PRIMITIVE_EXACT_BOUNDS_FLAG);

  if (f0 || f1 || f2) {
    transact(doc, CommandId::SetPrimitiveProperties, [&] {
      primitive->setFlags((uint32_t)flags);
    });
  }

  auto timeUpdateDist = primitive->getTimeUpdateDistance();

  widgets::HelpMarker("Distance within which Primitive Time updates.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  if (ImGui::InputFloat("Time update distance", &timeUpdateDist, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (timeUpdateDist >= 0.0f) {
      transact(doc, CommandId::SetPrimitiveProperties, [&] {
        primitive->setTimeUpdateDistance(timeUpdateDist);
      });
    }
  }
}

void renderEditPrimitiveProperties(editor::Document* doc, bw::core::Primitive* primitive, editor::Settings& settings) {
  // Properties
  auto properties = primitive->getProperties();
  auto updateProperties = renderPrimitivePropertySet(
      &properties, true, doc, settings, primitive);

  // Update
  if (updateProperties) {
    transact(doc, CommandId::SetPrimitiveProperties, [&] {
      primitive->setProperties(properties);
    });
  }
}

void renderEditPrimitiveAudioEmitters(
    editor::Document* doc, bw::core::Primitive* primitive) {
  if (ImGui::Button("Add emitter")) {
    transact(doc, CommandId::AddPrimitiveAudioEmitter, [&] { addPrimitiveAudioEmitter(doc, primitive); });
  }

  auto const emitters = primitive->getAudioEmitters();
  for (uint32_t i = 0; i < emitters.size(); ++i) {
    auto const& emitter = emitters[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::SeparatorText(format("Emitter {}", i).c_str());

    if (ImGui::Button("Delete")) {
      transact(doc, CommandId::DeletePrimitiveAudioEmitter, [&] { deletePrimitiveAudioEmitter(doc, primitive, i); });
      ImGui::PopID();
      break;
    }

    float offset[2]{emitter.offset.x, emitter.offset.y};
    ImGui::SetNextItemWidth(192.0f);
    if (ImGui::InputFloat2("Offset", offset)) {
      transact(doc, CommandId::SetPrimitiveAudioEmitterOffset, [&] { setPrimitiveAudioEmitterOffset(doc, primitive, i, wp::Vector2{offset[0], offset[1]}); });
    }

    auto floorOffset = emitter.heightOffset;
    ImGui::SetNextItemWidth(128.0f);
    if (ImGui::InputFloat("Floor offset", &floorOffset)) {
      transact(doc, CommandId::SetPrimitiveAudioEmitterHeightOffset, [&] { setPrimitiveAudioEmitterHeightOffset(doc, primitive, i, floorOffset); });
    }

    auto soundId = emitter.soundId;
    ImGui::SetNextItemWidth(256.0f);
    if (widgets::InputText(
            "soundId", &soundId, ImGuiInputTextFlags_EnterReturnsTrue)) {
      transact(doc, CommandId::SetPrimitiveAudioEmitterSoundId, [&] { setPrimitiveAudioEmitterSoundId(doc, primitive, i, soundId); });
    }

    ImGui::PopID();
  }
}

void renderEditPrimitiveSettings(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveSettings(doc, world->getPrimitive(*selectedIndices.begin()), settings);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

void renderEditPrimitiveGeometry(editor::Document* doc, editor::Settings& settings, double globalTime) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveGeometry(doc, world->getPrimitive(*selectedIndices.begin()), settings, globalTime);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

void renderEditPrimitiveProperties(editor::Document* doc, editor::Settings& settings) {
  auto world = doc->getWorld();
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  switch (selectedIndices.size()) {
    case 0:
      break;

    case 1:
      renderEditPrimitiveProperties(doc, world->getPrimitive(*selectedIndices.begin()), settings);
      break;

    default:
      ImGui::Text("Multiple primitives selected.");
      break;
  }
}

void renderEditPrimitiveAudioEmitters(editor::Document* doc) {
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();
  if (selectedIndices.size() == 1) {
    renderEditPrimitiveAudioEmitters(
        doc, doc->getWorld()->getPrimitive(*selectedIndices.begin()));
  } else if (selectedIndices.size() > 1) {
    ImGui::Text("Multiple primitives selected.");
  }
}

// Whether the Edit Primitive view has anything at all to show. Document's
// hasSelection() is not the question: it is also true for a TriggerLine or a
// world vertex. Nor is a selected ghost, which is authoring furniture with
// nothing to edit - and being index 0 it sorts first, so it is what the view
// would otherwise reach for.
bool hasEditablePrimitiveSelection(editor::Document* doc) {
  auto const& selectedIndices = doc->getSelectedPrimitiveIndices();

  return !selectedIndices.empty() &&
         *selectedIndices.begin() != uint32_t(ED_GHOST_INDEX);
}

void renderEditPrimitiveView(ViewContext& context) {
  auto* doc = context.doc;
  auto& settings = context.settings;
  auto globalTime = context.globalTime;
  if (!hasEditablePrimitiveSelection(doc)) {
    return;
  }

  ImGui::SeparatorText("Settings");
  renderEditPrimitiveSettings(doc, settings);

  ImGui::SeparatorText("Geometry");
  renderEditPrimitiveGeometry(doc, settings, globalTime);

  ImGui::SeparatorText("Properties");
  renderEditPrimitiveProperties(doc, settings);

  ImGui::SeparatorText("Audio Emitters");
  renderEditPrimitiveAudioEmitters(doc);
}


}  // namespace editor
