#define NOMINMAX

#include "UiInternal.h"
#include "imgui_curve.hpp"

namespace editor {
using namespace std;

void renderTransformFlow(editor::Document* doc, bw::core::Primitive* primitive, vector<bw::core::tTransform> const& flow, bw::core::VertexTransformer::Key key, string const& keyName) {
  ImGui::Text("%s Transform Flow", keyName.c_str());

  char const* operandTypes[] = {"Input", "Constant", "Sine", "Inv Cosine", "Triangle", "Saw", "Square", "TriggerLine (both)", "TriggerLine (red)", "TriggerLine (blue)", "Previous"};
  char const* inputTypes[] = {"Eye dist", "Eye angle", "Global angle", "Player move", "Player turn", "Player move/turn", "User 1", "User 2", "User 3", "User 4"};

  for (uint32_t i = 0; i < (uint32_t)flow.size(); ++i) {
    ImGui::PushID(i);

    auto& transform = flow[i];

    int operand0 = (int)transform.operands[0];
    int operand1 = (int)transform.operands[1];
    float constant0 = transform.constants[0];
    float constant1 = transform.constants[1];
    float fnMultiplier0 = transform.fnMultipliers[0];
    float fnMultiplier1 = transform.fnMultipliers[1];
    int index0 = (int)transform.indices[0];
    int index1 = (int)transform.indices[1];
    int input0 = (int)transform.inputs[0];
    int input1 = (int)transform.inputs[1];
    int operation = (int)transform.operation;

    // Don't allow "previous" on first entry in flow
    auto numOperandTypes = IM_ARRAYSIZE(operandTypes);

    if (i == 0) {
      numOperandTypes--;
    }

    // Operand 1 type
    widgets::HelpMarker("Type of value to use for the left side of the transform equation.  Either a pre-defined input source, a constant value, or the result of the previous calculation in the transform.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96);
    if (ImGui::Combo("##Op1TransformFlow", &operand0, operandTypes, numOperandTypes)) {
      transact(doc, "Set Transform Operand 1", [&] {
        setTransformOperand(doc, primitive, key, i, 0, (bw::core::tTransform::OperandType)operand0);
      });
    }

    ImGui::SameLine();

    // Operand 1 value
    widgets::HelpMarker("Value to use for the left side of the transform equation.  If 'Input' was selected as type then the selected value will take the incoming output from the relevant Input interpolator.  If 'Constant' was chosen, then enter a value in [0, 1].");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112);
    switch ((bw::core::tTransform::OperandType)operand0) {
      case bw::core::tTransform::OperandType::Input:
        if (ImGui::Combo("##In1TransformFlow", &input0, inputTypes, IM_ARRAYSIZE(inputTypes))) {
          transact(doc, "Set Transform Input 1", [&] {
            setTransformInput(doc, primitive, key, i, 0, (bw::core::InputType)input0);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Constant:
        if (ImGui::InputFloat("##Cn1TransformFlow", &constant0, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          transact(doc, "Set Transform Constant 1", [&] {
            // float c = clamp(constant0, ED_MIN_TRANSFORM_CONSTANT, ED_MAX_TRANSFORM_CONSTANT);
            float c = constant0;
            setTransformConstant(doc, primitive, key, i, 0, c);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Sine:
      case bw::core::tTransform::OperandType::InvCosine:
      case bw::core::tTransform::OperandType::Triangle:
      case bw::core::tTransform::OperandType::Saw:
      case bw::core::tTransform::OperandType::Square:
        if (ImGui::InputFloat("##Fn1TransformFlow", &fnMultiplier0, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (fnMultiplier0 > 0.0f) {
            transact(doc, "Set Transform Function 1", [&] {
              float c = fnMultiplier0;
              setTransformFnMultiplier(doc, primitive, key, i, 0, c);
            });
          }
        }
        break;

      case bw::core::tTransform::OperandType::TriggerLine:
      case bw::core::tTransform::OperandType::TriggerLineRed:
      case bw::core::tTransform::OperandType::TriggerLineBlue:
        if (ImGui::InputInt("##Tr1TransformFlow", &index0, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
          transact(doc, "Set Transform Index 1", [&] {
            uint32_t i0 = max(0, index0);
            setTransformTriggerLine(doc, primitive, key, i, 0, i0);
          });
        }
        break;

      case bw::core::tTransform::OperandType::TransformOutput:
        ImGui::Text("<result>");
        break;
    }

    ImGui::SameLine();

    // Operation
    char const* opTypes[] = {"+", "*", "|-|", "Min", "Max", "Avg", "<", ">", "<=", ">=", "%/"};

    widgets::HelpMarker("Operator to use for the transform equation.  |-| means the absolute difference of the two values.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    if (ImGui::Combo("##OpTransformFlow", &operation, opTypes, IM_ARRAYSIZE(opTypes))) {
      transact(doc, "Set Transform Operation", [&] {
        setTransformOperation(doc, primitive, key, i, (bw::core::tTransform::Operation)operation);
      });
    }

    ImGui::SameLine();

    // Operand 2 type
    widgets::HelpMarker("Type of value to use for the right side of the transform equation.  Either a pre-defined input source, a constant value, or the result of the previous calculation in the transform.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96);
    if (ImGui::Combo("##Op2TransformFlow", &operand1, operandTypes, numOperandTypes)) {
      transact(doc, "Set Transform Operand 1", [&] {
        setTransformOperand(doc, primitive, key, i, 1, (bw::core::tTransform::OperandType)operand1);
      });
    }

    ImGui::SameLine();

    // Operand 2 value
    widgets::HelpMarker("Value to use for the right side of the transform equation.  If 'Input' was selected as type then the selected value will take the incoming output from the relevant Input interpolator.  If 'Constant' was chosen, then enter a value in [0, 1].");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(112);
    switch ((bw::core::tTransform::OperandType)operand1) {
      case bw::core::tTransform::OperandType::Input:
        if (ImGui::Combo("##In2TransformFlow", &input1, inputTypes, IM_ARRAYSIZE(inputTypes))) {
          transact(doc, "Set Transform Input 2", [&] {
            setTransformInput(doc, primitive, key, i, 1, (bw::core::InputType)input1);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Constant:
        if (ImGui::InputFloat("##Cn2TransformFlow", &constant1, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          transact(doc, "Set Transform Constant 2", [&] {
            // float c = clamp(constant1, ED_MIN_TRANSFORM_CONSTANT, ED_MAX_TRANSFORM_CONSTANT);
            float c = constant1;

            setTransformConstant(doc, primitive, key, i, 1, c);
          });
        }
        break;

      case bw::core::tTransform::OperandType::Sine:
      case bw::core::tTransform::OperandType::InvCosine:
      case bw::core::tTransform::OperandType::Triangle:
      case bw::core::tTransform::OperandType::Saw:
      case bw::core::tTransform::OperandType::Square:
        if (ImGui::InputFloat("##Fn2TransformFlow", &fnMultiplier1, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (fnMultiplier1 > 0.0f) {
            transact(doc, "Set Transform Function 2", [&] {
              float c = fnMultiplier1;
              setTransformFnMultiplier(doc, primitive, key, i, 1, c);
            });
          }
        }
        break;

      case bw::core::tTransform::OperandType::TriggerLine:
      case bw::core::tTransform::OperandType::TriggerLineRed:
      case bw::core::tTransform::OperandType::TriggerLineBlue:
        if (ImGui::InputInt("##Tr2TransformFlow", &index1, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
          transact(doc, "Set Transform Index 2", [&] {
            uint32_t i1 = max(0, index1);
            setTransformTriggerLine(doc, primitive, key, i, 1, i1);
          });
        }
        break;

      case bw::core::tTransform::OperandType::TransformOutput:
        ImGui::Text("<result>");
        break;
    }

    // Move up/down, delete
    ImGui::SameLine();

    int counter = 0;
    ImGui::PushButtonRepeat(false);

    if (i > 0) {
      ImGui::SameLine();
      if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
        counter--;
      }
    }

    if (i < (uint32_t)(flow.size() - 1)) {
      ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
      if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
        counter++;
      }
    }

    ImGui::PopButtonRepeat();

    if (counter < 0) {
      transact(doc, format("Swap {} Transforms", keyName), [&] { swapTransforms(doc, primitive, key, i, i - 1); });

    } else if (counter > 0) {
      transact(doc, format("Swap {} Transforms", keyName), [&] { swapTransforms(doc, primitive, key, i, i + 1); });
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_ERASER)) {
      transact(doc, format("Remove {} Transform", keyName), [&] { removeTransform(doc, primitive, key, i); });
    }

    ImGui::PopID();
  }

  if (ImGui::Button(ICON_FA_PLUS)) {
    transact(doc, format("Add {} Transform", keyName), [&] { addTransform(doc, primitive, key); });
  }
}

void renderTransformFlow(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  string keyName;

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      keyName = "Scale";
      break;

    case bw::core::VertexTransformer::Key::Angle:
      keyName = "Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      keyName = "Orbit Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      keyName = "Orbit Distance";
      break;
  }

  ImGui::PushID(keyName.c_str());

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      renderTransformFlow(doc, primitive, primitive->getScaleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::Angle:
      renderTransformFlow(doc, primitive, primitive->getAngleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      renderTransformFlow(doc, primitive, primitive->getOrbitAngleTransforms(), key, keyName);
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      renderTransformFlow(doc, primitive, primitive->getOrbitDistanceTransforms(), key, keyName);
      break;
  }

  ImGui::PopID();
}

vector<string> gEasingStrings = {
    "Linear",
    "EaseInSine",
    "EaseInCubic",
    "EaseInQuintic",
    "EaseOutSine",
    "EaseOutCubic",
    "EaseOutQuintic",
    "EaseInOutSine",
    "EaseInOutCubic",
    "EaseInOutQuintic",
    "EaseInBack",
    "EaseOutBack",
    "EaseInOutBack",
    "EaseInExpo",
    "EaseOutExpo",
    "EaseInOutExpo",
    "EaseInElastic",
    "EaseOutElastic",
    "EaseInOutElastic",
    "EaseInBounce",
    "EaseOutBounce",
    "EaseInOutBounce"};

typedef function<void()> AdditionalWidgetsFunction;

void renderInterpolator(editor::Document* doc, bw::core::Primitive* primitive, bw::core::Interpolator<float> const& lerper, bw::core::VertexTransformer::Key key, int curveIndex, string const& name, string const& lerperType, float proxyValue, float curValue, AdditionalWidgetsFunction addWidgetFunc = {}) {
  string easingsStr, lockTypesStr;

  for (auto const& easing : gEasingStrings) {
    easingsStr += easing;
    easingsStr += '\0';
  }

  vector<bw::core::Interpolator<float>::Point> points = lerper.getPoints();
  vector<vector<bw::core::Interpolator<float>::Point>> renderValues = lerper.render(100.0f);

  wp::Vector2 scaleMin, scaleMax;
  lerper.getScale(&scaleMin, &scaleMax);

  ImGui::PushID(name.c_str());

  ImGui::TextUnformatted(name.c_str());

  if (addWidgetFunc) {
    addWidgetFunc();
  }

  auto numPoints = (uint32_t)points.size();

  array<ImVec2, bw::core::Interpolator<float>::MaxPoints> imPoints;
  for (uint32_t i = 0; i < numPoints; ++i) {
    imPoints[i] = {points[i].first, points[i].second};
  }

  int nPoints = (int)numPoints, editPoint;
  int curveWidgetWidth = 0;
  bool clicked, released;

  if (ImGui::MultiCurve(name.c_str(),
                        imPoints.data(),
                        &nPoints,
                        bw::core::Interpolator<float>::MaxPoints,
                        &editPoint,
                        false,
                        scaleMin.x,
                        scaleMin.y,
                        scaleMax.x,
                        scaleMax.y,
                        renderValues,
                        proxyValue,
                        curValue,
                        128,
                        &curveWidgetWidth,
                        &clicked,
                        &released)) {
    // We've either created, deleted or moved a point, depending on the new size.
    // So we may need to update the segments
    if (nPoints > (int)numPoints) {
      auto const& p = imPoints[editPoint];
      transact(doc, format("Add {} Point at {:.2f}", name, p.x), [&] { addKeyToInterpolator(doc, lerperType, primitive, key, p.x, p.y); });
    } else if (nPoints < (int)numPoints) {
      transact(doc, format("Remove {} Point {}", name, editPoint), [&] { removeKeyFromInterpolator(doc, lerperType, primitive, key, editPoint); });
    } else {
      wp::Vector2 editValue = {imPoints[editPoint].x, imPoints[editPoint].y};

      if (clicked) {
        beginTransaction(doc, "Move point", editValue);
      }

      // Moved. The undoable action only commits on release, so this drag is
      // the one path that changes an animated property without passing
      // through commitUndoableAction's regeneration. Ask for it here so the
      // world follows the curve as it is dragged; requests coalesce, so a
      // per-frame ask costs at most one extra generation.
      updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, editPoint, editValue.x, editValue.y);
      regenerateWorldData(doc);
    }
  }

  if (released) {
    if (editPoint >= 0) {
      wp::Vector2 editValue = {imPoints[editPoint].x, imPoints[editPoint].y};
      if (editor::transactionValueHasChanged(editValue)) {
        commitUndoableAction(doc);
      } else {
        // Dragged back to where it started: nothing to record, but the
        // transaction opened on click must not be left in progress.
        abandonUndoableAction(doc);
      }
    } else {
      abandonUndoableAction(doc);
    }
  }

  // Segments
  int pointToRemove{-1};
  {
    points = lerper.getPoints();
    vector<bw::core::Interpolator<float>::Segment> const& segments = lerper.getSegments();

    auto numPoints = (uint32_t)points.size();
    auto numSegments = (uint32_t)segments.size();

    for (uint32_t i = 0; i < numSegments; ++i) {
      ImGui::PushID(i);

      if (ImGui::Button(ICON_FA_ERASER)) {
        pointToRemove = (int)i;
      }

      // Ignore segments where diff(x) is 0
      bool disable = i < numSegments && points[i].first == points[i + 1].first;

      if (disable) {
        widgets::PushDisabled();
      }

      auto& segment = segments[i];

      ImGui::SameLine();

      widgets::HelpMarker("Set the easing function to use when interpolating between the points.");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(128);
      int curEasing = (int)segment.easing;
      if (ImGui::Combo("###Interpolator", &curEasing, easingsStr.c_str(), 6)) {
        transact(doc, "Set Interpolator Segment Easing", [&] {
          setInterpolatorEasing(doc, lerperType, primitive, key, i, (bw::core::Easing)curEasing);
        });
      }

      ImGui::SameLine();

      widgets::HelpMarker("Manual specification of point values.  Note that discontinuous segments are disabled and need to be moved to be editable.");
      ImGui::SameLine();
      ImGui::SetNextItemWidth((float)curveWidgetWidth - 256);
      float inputValues[4] = {points[i + 0].first, points[i + 0].second, points[i + 1].first, points[i + 1].second};
      if (ImGui::InputFloat4("##Interpolator", inputValues, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Clamp to neighbours to ensure we don't end up with non-ascending values
        if (i == 0) {
          // x value can't be changed
          inputValues[0] = scaleMin.x;
        } else {
          inputValues[0] = max(points[i - 1].first, inputValues[0]);
        }

        inputValues[0] = min(points[i + 1].first, inputValues[0]);
        inputValues[2] = max(inputValues[0], inputValues[2]);

        if (i == (numSegments - 1)) {
          // x value can't be changed
          inputValues[2] = scaleMax.x;
        } else {
          inputValues[2] = min(points[i + 2].first, inputValues[2]);
        }

        // Clamp to defined extents
        inputValues[0] = clamp(inputValues[0], scaleMin.x, scaleMax.x);
        inputValues[1] = clamp(inputValues[1], scaleMin.y, scaleMax.y);
        inputValues[2] = clamp(inputValues[2], scaleMin.x, scaleMax.x);
        inputValues[3] = clamp(inputValues[3], scaleMin.y, scaleMax.y);

        transact(doc, "Update Points", [&] {
          updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, i, inputValues[0], inputValues[1]);
          updateAnimationKeyInInterpolator(doc, lerperType, primitive, key, i + 1, inputValues[2], inputValues[3]);
        });
      }

      if (disable) {
        widgets::PopDisabled();
      }

      ImGui::PopID();
    }
  }

  ImGui::PopID();

  if (pointToRemove != -1) {
    transact(doc, format("Remove {} Point {}", lerperType, pointToRemove), [&] { removeKeyFromInterpolator(doc, lerperType, primitive, key, (uint32_t)pointToRemove); });
  }
}

void renderInterpolator(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, bw::core::Interpolator<float> const& interpolator, string const& lerperType, float proxyValue, float curValue) {
  string keyName;

  switch (key) {
    case bw::core::VertexTransformer::Key::Scale:
      keyName = "Scale";
      break;

    case bw::core::VertexTransformer::Key::Angle:
      keyName = "Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitAngle:
      keyName = "Orbit Angle";
      break;

    case bw::core::VertexTransformer::Key::OrbitDistance:
      keyName = "Orbit Distance";
      break;

    default:
      throw EditorException("Unknown transform key");
  }

  renderInterpolator(doc, primitive, interpolator, key, (int)key + 3, format("{} {}", keyName, lerperType), lerperType, proxyValue, curValue);
}

void renderValueCapture(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  vector<string> captureModes = {
      "Distance/Sticky",
      "Distance/Delta Up",
      "Distance/Delta Down",
      "Distance/Latched Up",
      "Distance/Latched Down",
      "Angle/Sticky",
      "Angle/Delta Up",
      "Angle/Delta Down",
      "Angle/Latched Up",
      "Angle/Latched Down"};

  string captureModesStr;

  for (auto const& captureMode : captureModes) {
    captureModesStr += captureMode;
    captureModesStr += '\0';
  }

  widgets::HelpMarker("Capture mode for value.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(128);

  int selectedMode = (int)primitive->getCaptureMode(key);
  if (ImGui::Combo("Capture mode", &selectedMode, captureModesStr.c_str(), 6)) {
    auto mode = (bw::core::ValueCaptureMode)selectedMode;
    transact(doc, "Set Capture Mode", [&] {
      setPrimitiveCaptureMode(doc, primitive, key, mode);
    });
  }
}

void renderAnimatedPropertyEvents(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key) {
  ImGui::Text("Events");

  widgets::HelpMarker("Add new event.");
  ImGui::SameLine();

  if (ImGui::Button(ICON_FA_PLUS)) {
    transact(doc, "Add Animated Property Event", [&] {
      addPrimitiveAnimatedPropertyEvent(doc, primitive, key, 1, bw::core::AnimatedPropertyEventTriggerType::UpDown, 0.5f);
    });
  }

  vector<string> triggerTypes = {
      "Up",
      "Down",
      "Up or down"};

  vector<string> eventTypes = {
      "Debug",
      "Gen clip"};

  string triggerTypesStr, eventTypesStr;

  for (auto const& triggerType : triggerTypes) {
    triggerTypesStr += triggerType;
    triggerTypesStr += '\0';
  }

  for (auto const& eventType : eventTypes) {
    eventTypesStr += eventType;
    eventTypesStr += '\0';
  }

  int indexToDelete{-1};
  auto const& events = primitive->getAnimatedPropertyEvents(key);

  for (uint32_t i = 0; i < (uint32_t)events.size(); ++i) {
    auto const& event = events[i];

    widgets::HelpMarker("Delete event.");
    ImGui::SameLine();

    if (ImGui::Button(ICON_FA_ERASER)) {
      indexToDelete = (int)i;
    }

    ImGui::SameLine();

    widgets::HelpMarker("Trigger type.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    int triggerType = (int)event.triggerType;

    if (ImGui::Combo("Trigger", &triggerType, triggerTypesStr.c_str(), 6)) {
      transact(doc, "Set Primitive Event Trigger", [&] { setPrimitiveAnimatedPropertyEvent(doc, primitive, key, i, event.eventType, (bw::core::AnimatedPropertyEventTriggerType)triggerType, event.value); });
    }

    ImGui::SameLine();

    widgets::HelpMarker("Event type.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(128);
    int eventType = (int)log2(event.eventType);  // eventType is a bitmask, not an index

    if (ImGui::Combo("Action", &eventType, eventTypesStr.c_str(), 6)) {
      transact(doc, "Set Primitive Event Action", [&] { setPrimitiveAnimatedPropertyEvent(doc, primitive, key, i, 1 << eventType, event.triggerType, event.value); });
    }

    ImGui::SameLine();

    widgets::HelpMarker("Trigger value.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    float value = event.value;
    if (ImGui::InputFloat("Value", &value, 0.0f, 0.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
      transact(doc, "Set Primitive Event Value", [&] { setPrimitiveAnimatedPropertyEvent(doc, primitive, key, i, event.eventType, event.triggerType, value); });
    }
  }

  if (indexToDelete >= 0) {
    transact(doc, "Delete Primitive Event", [&] { deletePrimitiveAnimatedPropertyEvent(doc, primitive, key, (uint32_t)indexToDelete); });
  }
}

void renderAnimatedProperty(editor::Document* doc, bw::core::Primitive* primitive, bw::core::VertexTransformer::Key key, editor::Settings& settings, double globalTime) {
  ImGui::PushID((int)key);

  widgets::HelpMarker("Reset animator.");
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    // Resets captured animator state rather than authored data, so it is not
    // undoable - but it still changes the value this key contributes to the
    // fold, so the world it produced is now stale.
    primitive->resetAnimator(key);
    regenerateWorldData(doc);
  }

  ImGui::Separator();

  renderTransformFlow(doc, primitive, key);
  ImGui::Separator();

  float proxyValue = primitive->transformT(key, globalTime);
  float curValue = primitive->getCurCapturedValue(key);
  bw::core::Primitive const* constPrim = primitive;

  renderInterpolator(doc, primitive, key, constPrim->getAnimationInterpolator(key), "Animation", proxyValue, curValue);
  ImGui::Separator();

  float inflPreview = (BW_INTERPOLATOR_MAX_DISTANCE - primitive->getInputs().entityInfluenceDistance) / BW_INTERPOLATOR_MAX_DISTANCE;
  renderInterpolator(doc, primitive, key, constPrim->getInfluenceInterpolator(key), "Influence", inflPreview, -1.0f);
  ImGui::Separator();

  renderValueCapture(doc, primitive, key);
  ImGui::Separator();

  renderAnimatedPropertyEvents(doc, primitive, key);

  ImGui::PopID();
}


}  // namespace editor
