#pragma once

#include <vector>

#include <mpp/helper/QuadBatchDataProvider.h>

#include <willpower/application/resourcesystem/Resource.h>

#include "Platform.h"
#include "Battery.h"
#include "Bullet.h"

namespace applib
{

	class APPLIB_API BulletRenderDataProvider : public mpp::helper::QuadBatchDataProvider<mpp::mesh::DataTypeFloat, mpp::mesh::DataTypeFloat>
	{
		Battery<Bullet>* mwBattery;

		wp::application::resourcesystem::ResourcePtr mVisual;

	public:

		BulletRenderDataProvider(Battery<Bullet>* battery, wp::application::resourcesystem::ResourcePtr visual);

		void getBounds(glm::vec3& bMin, glm::vec3& bMax) override;

		void position(uint32_t index, float& x, float& y) override;

		void angle(uint32_t index, float& angle) override;

		void direction(uint32_t index, float& x, float& y) override;

		void textureAtlasTexcoords(uint32_t index, float& u0, float& v0, float& u1, float& v1) override;

		void dimensions(uint32_t index, float& halfWidth, float& halfHeight) override;

		mpp::Colour diffuse() override;

		bool update(float frameTime) override;
	};

} // applib