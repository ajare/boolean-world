#include <cmath>

#include "core/TransformFlow.h"
#include "core/Defines.h"

namespace {
constexpr float MinimumFunctionMultiplier = 0.0001f;

float functionMultiplier(float value) {
  return std::isfinite(value) && value > MinimumFunctionMultiplier
             ? value
             : MinimumFunctionMultiplier;
}
}  // namespace

namespace bw {
namespace core {
using namespace std;

TransformFlow::TransformFlow() {
  mTransforms = {
      tTransform::makePassthroughInput(InputType::InfluenceEyeDistance)};
}

TransformFlow::TransformFlow(TransformFlow const& other) {
  copyFrom(other);
}

TransformFlow& TransformFlow::operator=(TransformFlow const& other) {
  copyFrom(other);
  return *this;
}

void TransformFlow::copyFrom(TransformFlow const& other) {
  mTransforms = other.mTransforms;
}

bool TransformFlow::childrenModified() const {
  for (auto const& transform : mTransforms) {
    if (transform.isModified()) {
      return true;
    }
  }

  return false;
}

void TransformFlow::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const {
  serializer->beginMap("transformFlow");
  {
    serializer->beginArray("transforms");
    {
      for (auto const& transform : mTransforms) {
        transform.serialize(serializer, workData);
      }

      serializer->endArray();  // transforms
    }

    serializer->endMap();  // transformFlow
  }
}

bool TransformFlow::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) {
  vector<tTransform> transforms;

  try {
    serializer->beginMap("transformFlow");
    {
      serializer->beginArray("transforms");
      {
        while (serializer->nextArrayItem()) {
          tTransform transform;

          if (!transform.deserialize(serializer, workData)) {
            copyErrorsAndWarnings(&transform, true, true);
            return false;
          }

          transforms.push_back(transform);
        }

        serializer->endArray();  // transforms
      }

      serializer->endMap();  // transformFlow
    }
  } catch (exception& e) {
    addDeserializationError(e.what());
    return false;
  }

  // Commit
  mTransforms = transforms;

  return true;
}

float TransformFlow::triangle(double time, float value) const {
  float u = (float)fmod(time, value) / value;

  return u < 0.5f ? u * 2 : 2 - u * 2;
}

float TransformFlow::saw(double time, float value) const {
  float u = (float)fmod(time, value) / value;

  return u;
}

float TransformFlow::square(double time, float value) const {
  float u = (float)fmod(time, value) / value;

  return u < 0.5f ? 0.0f : 1.0f;
}

float TransformFlow::transformT(InputValue const& inputs, double time) const {
  float value{0.0f};
  for (auto const& transform : mTransforms) {
    // Transform value
    float operands[2] = {0.0f, 0.0f};

    for (int i = 0; i < 2; ++i) {
      switch (transform.operands[i]) {
        case tTransform::OperandType::Input:
          switch (transform.inputs[i]) {
            case InputType::InfluenceEyeDistance:
              operands[i] = 1.0f - (inputs.entityInfluenceDistance / BW_INTERPOLATOR_MAX_DISTANCE);
              break;

            case InputType::InfluenceEyeAngle:
              operands[i] = inputs.entityInfluenceAngle / BW_INTERPOLATOR_MAX_ANGLE;
              break;

            case InputType::PlayerAngle:
              operands[i] = inputs.entityGlobalAngle / BW_INTERPOLATOR_MAX_ANGLE;
              break;

            case InputType::PlayerMove:
              operands[i] = inputs.playerMove ? 1.0f : 0.0f;
              break;

            case InputType::PlayerTurn:
              operands[i] = inputs.playerTurn ? 1.0f : 0.0f;
              break;

            case InputType::PlayerMoveOrTurn:
              operands[i] = (inputs.playerMove || inputs.playerTurn) ? 1.0f : 0.0f;
              break;

            case InputType::User1:
              operands[i] = inputs.user[0];
              break;

            case InputType::User2:
              operands[i] = inputs.user[1];
              break;

            case InputType::User3:
              operands[i] = inputs.user[2];
              break;

            case InputType::User4:
              operands[i] = inputs.user[3];
              break;

            default:
              operands[i] = 0.0f;
              break;
          }
          break;

        case tTransform::OperandType::Constant:
          operands[i] = transform.constants[i];
          break;

        case tTransform::OperandType::Sine: {
          auto multiplier = functionMultiplier(transform.fnMultipliers[i]);
          operands[i] = (float)(sin(time * BW_TWOPI / multiplier)) * 0.5f + 0.5f;
          break;
        }

        case tTransform::OperandType::InvCosine: {
          auto multiplier = functionMultiplier(transform.fnMultipliers[i]);
          operands[i] = 1.0f - ((float)(cos(time * BW_TWOPI / multiplier)) * 0.5f + 0.5f);
          break;
        }

        case tTransform::OperandType::Triangle:
          operands[i] = triangle(time, functionMultiplier(transform.fnMultipliers[i]));
          break;

        case tTransform::OperandType::Saw:
          operands[i] = saw(time, functionMultiplier(transform.fnMultipliers[i]));
          break;

        case tTransform::OperandType::Square:
          operands[i] = square(time, functionMultiplier(transform.fnMultipliers[i]));
          break;

        case tTransform::OperandType::TriggerLine:
          if (inputs.triggerLines) {
            operands[i] = (float)inputs.triggerLines->at(transform.indices[i])->getTotalTriggerCount();
          } else {
            operands[i] = 0.0f;
          }
          break;

        case tTransform::OperandType::TriggerLineRed:
          if (inputs.triggerLines) {
            operands[i] = (float)inputs.triggerLines->at(transform.indices[i])->getTriggerCount(WorldTriggerLineSide::Red);
          } else {
            operands[i] = 0.0f;
          }
          break;

        case tTransform::OperandType::TriggerLineBlue:
          if (inputs.triggerLines) {
            operands[i] = (float)inputs.triggerLines->at(transform.indices[i])->getTriggerCount(WorldTriggerLineSide::Blue);
          } else {
            operands[i] = 0.0f;
          }
          break;

        case tTransform::OperandType::TransformOutput:
          operands[i] = value;
          break;

        default:
          operands[i] = 0.0f;
          break;
      }
    }

    switch (transform.operation) {
      case tTransform::Operation::Add:
        value = clamp(operands[0] + operands[1], 0.0f, 1.0f);
        break;
      case tTransform::Operation::Mul:
        value = clamp(operands[0] * operands[1], 0.0f, 1.0f);
        break;
      case tTransform::Operation::AbsDiff:
        value = clamp(fabs(operands[0] - operands[1]), 0.0f, 1.0f);
        break;
      case tTransform::Operation::Min:
        value = clamp(min(operands[0], operands[1]), 0.0f, 1.0f);
        break;
      case tTransform::Operation::Max:
        value = clamp(max(operands[0], operands[1]), 0.0f, 1.0f);
        break;
      case tTransform::Operation::Avg:
        value = clamp((operands[0] + operands[1]) * 0.5f, 0.0f, 1.0f);
        break;
      case tTransform::Operation::Less:
        value = operands[0] < operands[1] ? 1.0f : 0.0f;
        break;
      case tTransform::Operation::Greater:
        value = operands[0] > operands[1] ? 1.0f : 0.0f;
        break;
      case tTransform::Operation::LessEq:
        value = operands[0] <= operands[1] ? 1.0f : 0.0f;
        break;
      case tTransform::Operation::GreaterEq:
        value = operands[0] >= operands[1] ? 1.0f : 0.0f;
        break;
      case tTransform::Operation::ModNormalise:
        if (operands[1] == 0.0f) {
          operands[1] = 1.0f;
        }

        value = fmod(operands[0], operands[1]) / operands[1];
        break;

      default:
        value = 0.0f;
        break;
    }
  }

  return clamp(value, 0.0f, 1.0f);
}

void TransformFlow::setTransforms(vector<tTransform> const& transforms) {
  mTransforms = transforms;
}

vector<tTransform>& TransformFlow::getTransforms() {
  return mTransforms;
}

vector<tTransform> const& TransformFlow::getTransforms() const {
  return mTransforms;
}

}  // namespace core
}  // namespace bw