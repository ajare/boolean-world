#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "Settings.h"

namespace bw::core {
class World;
}

namespace editor {

// Owns the editor's index-based selection and hover state. Geometry and
// authored objects remain owned by the document; Selection only records which
// of those objects are selected.
class Selection {
protected:
  std::set<uint32_t> mSelectedPrimitiveIndices;
  uint32_t mSelectedWorldVertexIndex{~0u};
  uint32_t mSelectedTriggerLineIndex{~0u};
  std::set<uint32_t> mSelectedMeshVertexIndices;
  std::set<uint32_t> mSelectedMeshEdgeIndices;
  std::set<uint32_t> mSelectedMeshRingIndices;
  std::string mMeshHoverExplanation;

  [[nodiscard]] virtual bw::core::World const* selectionWorld() const = 0;

public:
  virtual ~Selection() = default;

  void setSelectedWorldVertexIndex(uint32_t index);
  void setSelectedTriggerLineIndex(uint32_t index);
  void setSelectedPrimitiveIndices(std::set<uint32_t> const& indices);
  void addSelectedPrimitiveIndex(uint32_t index);
  void addSelectedPrimitiveIndices(std::set<uint32_t> const& indices);
  void removeSelectedPrimitiveIndex(uint32_t index);
  void removeSelectedPrimitiveIndices(std::set<uint32_t> const& indices);
  void clearSelections();
  void clearMeshSelections();
  void revalidateSelection();

  [[nodiscard]] std::set<uint32_t> const& getSelectedPrimitiveIndices() const;
  [[nodiscard]] bool indexInSelection(uint32_t index) const;
  [[nodiscard]] bool anyPrimitiveIndicesSelected(std::vector<uint32_t> const& indices) const;
  [[nodiscard]] uint32_t getSelectedWorldVertexIndex() const;
  [[nodiscard]] uint32_t getSelectedTriggerLineIndex() const;
  [[nodiscard]] bool hasSelection() const;

  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode) const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshVertexIndices() const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshEdgeIndices() const;
  [[nodiscard]] std::set<uint32_t> const& getSelectedMeshRingIndices() const;
  void setSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);
  void addSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);
  void toggleSelectedMeshSubObjectIndices(
      Settings::MeshSubMode subMode, std::set<uint32_t> const& indices);

  void setMeshHoverExplanation(std::string explanation);
  [[nodiscard]] std::string const& getMeshHoverExplanation() const;
};

}  // namespace editor
