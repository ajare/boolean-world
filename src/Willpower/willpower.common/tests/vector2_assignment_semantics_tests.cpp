#include <concepts>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <willpower/common/Vector2.h>

namespace {

using Vector2 = wp::Vector2;

static_assert(std::same_as<decltype(std::declval<Vector2&>() = std::declval<Vector2 const&>()),
                           Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() += std::declval<Vector2 const&>()),
                           Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() += 1.0f), Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() -= std::declval<Vector2 const&>()),
                           Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() -= 1.0f), Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() *= std::declval<Vector2 const&>()),
                           Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() *= 1.0f), Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() /= std::declval<Vector2 const&>()),
                           Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() /= 1.0f), Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() /= 1), Vector2&>);
static_assert(std::same_as<decltype(std::declval<Vector2&>() /= 1u), Vector2&>);

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void assignmentAndCompoundAssignmentModifyTheirLeftOperand() {
  Vector2 assigned(1.0f, 2.0f);
  Vector2 const source(3.0f, 4.0f);
  (assigned = source) = Vector2(5.0f, 6.0f);
  require(assigned == Vector2(5.0f, 6.0f),
          "nested assignment must modify the original left operand");

  Vector2 accumulated(1.0f, 2.0f);
  (accumulated += Vector2(3.0f, 4.0f)) += Vector2(5.0f, 6.0f);
  require(accumulated == Vector2(9.0f, 12.0f),
          "chained compound assignment must modify the original left operand");
}

void vector2CompareIsLexicographic() {
  wp::Vector2Compare compare;
  require(compare(Vector2(1.0f, 2.0f), Vector2(2.0f, 1.0f)),
          "Vector2Compare must sort x coordinates ascending");
  require(!compare(Vector2(2.0f, 1.0f), Vector2(1.0f, 2.0f)),
          "Vector2Compare must not invert x-coordinate ordering");
  require(compare(Vector2(1.0f, 2.0f), Vector2(1.0f, 3.0f)),
          "Vector2Compare must sort equal x coordinates by y");
}

}  // namespace

int main() {
  try {
    assignmentAndCompoundAssignmentModifyTheirLeftOperand();
    vector2CompareIsLexicographic();
    std::cout << "Vector2 assignment semantics passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
