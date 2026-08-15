#pragma once

#include <string>
#include <set>
#include <map>

#include <willpower/common/Vector2.h>

#include "core/World.h"

#include "Settings.h"


namespace editor
{

	class Document
	{
		bool mModified;

		std::string mFilepath;

		std::shared_ptr<bw::core::World> mWorld;

		std::set<uint32_t> mSelectedPrimitiveIndices;

		uint32_t mSelectedWorldVertexIndex;

		uint32_t mSelectedTriggerLineIndex;

		wp::Vector2 mPlayerOldProxyPosition, mPlayerProxyPosition;

		float mPlayerOldProxyAngle, mPlayerProxyAngle;

		static Document* msInstance;

	private:

		void reset();

		std::shared_ptr<bw::core::World> createWorld(float size, float gridSize);

		void loadTiledPrefabFile(std::string const& filepath, std::shared_ptr<bw::core::World> world);

	public:

		Document();

		virtual ~Document();

		static Document* instance();

		bool isActive() const;

		void setModified(bool modified = true);

		bool isModified() const;

		std::string const& getFilepath() const;

		bool hasFilepath() const;

		void setWorld(bw::core::World const& world);

		std::shared_ptr<bw::core::World> getWorld();

		bw::core::Primitive* getGhost();

		void updateGhost(std::shared_ptr<bw::core::World> world, bw::core::Primitive* primitive);

		uint32_t getHoveredPrimitiveIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

		std::vector<uint32_t> getHoveredPrimitiveIndices(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

		bool indexInSelection(uint32_t index) const;

		void setSelectedWorldVertexIndex(uint32_t index);

		void setSelectedTriggerLineIndex(uint32_t index);

		void setSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

		void addSelectedPrimitiveIndex(uint32_t index);

		void addSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

		void removeSelectedPrimitiveIndex(uint32_t index);

		void removeSelectedPrimitiveIndices(std::set<uint32_t> const& indices);

		void clearSelections();

		std::set<uint32_t> const& getSelectedPrimitiveIndices() const;

		bool anyPrimitiveIndicesSelected(std::vector<uint32_t> const& indices) const;
		
		uint32_t getSelectedWorldVertexIndex() const;

		uint32_t getSelectedTriggerLineIndex() const;

		uint32_t getHoveredTriggerLineIndex(wp::Vector2 const& mouseWorldPos, Settings const& settings) const;

		bool hasSelection() const;

		void setPlayerProxyPosition(wp::Vector2 const& pos);

		wp::Vector2 const& getPlayerProxyPosition() const;

		wp::Vector2 const& getPlayerOldProxyPosition() const;

		void setPlayerProxyAngle(float angle);

		float getPlayerProxyAngle() const;

		float getPlayerOldProxyAngle() const;

		void newDoc();

		void closeDoc();

		bool openDoc(std::string const& filepath);

		void saveDoc();

		void saveDocAs(std::string const& filepath);

		void addPrefabInstance(bw::core::World const* prefab, int tileX, int tileY, float rotation, uint8_t layer);
	};

} // editor
