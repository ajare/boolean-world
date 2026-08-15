#pragma once

#include <vector>

#include <mpp/helper/LineBatchDataProvider.h>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/firepower/BeamShard.h>

#include "Platform.h"
#include "Battery.h"
#include "Beam.h"
#include "BeamInstance.h"

namespace applib
{

	class APPLIB_API BeamRenderDataProvider : public mpp::helper::LineBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeUnsignedByte>
	{
		struct Line
		{
			wp::Vector2 v0, v1;
			uint8_t r, g, b, a;
		};

	private:

		Battery<BeamInstance>* mwBattery;

		glm::vec3 mBounds[2];

		std::vector<Line> mLines;

	public:

		explicit BeamRenderDataProvider(Battery<BeamInstance>* battery);

		void getBounds(glm::vec3& bMin, glm::vec3& bMax) override;

		void position(uint32_t index, float& x0, float& y0, float& x1, float& y1) override;

		void colour(uint32_t index, uint8_t& red, uint8_t& green, uint8_t& blue, uint8_t& alpha) override;

		mpp::Colour diffuse() override;

		bool update(float frameTime) override;

		void clearData();

		void addData(std::vector<wp::firepower::BeamShard> const& shards);
	};

} // applib