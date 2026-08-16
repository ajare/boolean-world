#include <iostream>
#include <stdexcept>

#include <willpower/collide/Simulation.h>
#include <willpower/common/ExtentsCalculator.h>

namespace {

void require(bool condition, char const* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

wp::collide::Simulation createSimulation() {
  return wp::collide::Simulation(wp::ExtentsCalculator(wp::Vector2(0.0f, 0.0f), wp::Vector2(10.0f, 10.0f), 0.0f), 1, 1);
}

void coincidentCellBoundaryLineIsRegisteredWithEndpoints() {
  auto simulation = createSimulation();
  auto const* grid = simulation.getStaticLinesGrid();
  auto const gridOffset = grid->getOffset();
  auto const cellSize = grid->getCellSize();
  wp::Vector2 const start(gridOffset.x + cellSize.x, gridOffset.y + 1.0f);
  wp::Vector2 const end(start.x, gridOffset.y + cellSize.y - 1.0f);

  auto const [firstItem, numItems] = simulation.addStaticLine(start, end, false);

  require(numItems == 1, "A coincident cell-boundary line must be registered");
  auto const& line = simulation.getStaticLine(firstItem);
  require(line.getVertex(0) == start && line.getVertex(1) == end,
          "A coincident cell-boundary line must retain initialized endpoints");
}

void pointOnlyCellTouchIsNotRegistered() {
  auto simulation = createSimulation();

  auto const* grid = simulation.getStaticLinesGrid();
  auto const corner = grid->getOffset();
  auto const [firstItem, numItems] = simulation.addStaticLine(corner + wp::Vector2(-1.0f, 1.0f), corner + wp::Vector2(1.0f, -1.0f), false);

  require(firstItem == 0, "The first static-line index must remain unchanged for a point-only touch");
  require(numItems == 0, "A point-only cell touch must not register a static line");
  require(simulation.getNumStaticLines() == 0, "A point-only cell touch must not create a static line");
}

void backwardProjectedLineFindsStaticLine() {
  auto simulation = createSimulation();
  simulation.addStaticLine({5.0f, 0.0f}, {5.0f, 10.0f}, false);

  wp::collide::SweepResult result;
  auto const hit = simulation.projectLine({9.0f, 9.0f}, {1.0f, 1.0f}, &result);

  require(hit, "A backwards projected line must query its intersecting static line");
  require(result.newPosition.withinEpsilon({5.0f, 5.0f}),
          "A backwards projected line must stop at the static-line intersection");
}

}  // namespace

int main() {
  try {
    coincidentCellBoundaryLineIsRegisteredWithEndpoints();
    pointOnlyCellTouchIsNotRegistered();
    backwardProjectedLineFindsStaticLine();
    std::cout << "Static-line clipping passed\n";
    return 0;
  } catch (std::exception const& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
