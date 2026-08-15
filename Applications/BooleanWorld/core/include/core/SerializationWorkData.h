#pragma once

#include <vector>
#include <map>


namespace bw
{
	namespace core
	{
		class VertexTransformerObject;

		struct SerializationWorkData
		{
			// Size of grid to create when deserializing.  <= 0.0f means no grid
			float accelGridSize{ -1.0f };

			// Map VertexTransformer ids to their pointer
			std::map<uint32_t, VertexTransformerObject*> vtoIdToVtoMap;
			
			// Map VertexTransformer ids to their parent id
			std::map<uint32_t, int32_t> vtoIdToParentMap;
		};

	} // core
} // bw