#pragma once

#include "core/Platform.h"

namespace bw {
namespace core {

enum struct InputType {
  InfluenceEyeDistance,
  InfluenceEyeAngle,
  PlayerAngle,
  PlayerMove,
  PlayerTurn,
  PlayerMoveOrTurn,
  User1,
  User2,
  User3,
  User4,
  COUNT
};

}  // namespace core
}  // namespace bw
