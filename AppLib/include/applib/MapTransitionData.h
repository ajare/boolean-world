#pragma once

#include <memory>

#include <willpower/application/resourcesystem/Resource.h>

#include <willpower/collide/Simulation.h>

#include <willpower/firepower/MeshCollisionManager.h>

#include <willpower/viz/GeometryMeshRenderer.h>

namespace applib
{

	struct MapTransitionData
	{
		struct MapData
		{
			std::shared_ptr<wp::application::resourcesystem::Resource> map;
			std::unique_ptr<wp::viz::GeometryMeshRenderer> mapRenderer;
			std::unique_ptr<wp::collide::Simulation> mapCollisionSim;
			std::unique_ptr<wp::firepower::MeshCollisionManager> meshCollisionMgr;
		};

		MapData prevMap, nextMap;

		std::string nextMapName;
	};

}  // applib