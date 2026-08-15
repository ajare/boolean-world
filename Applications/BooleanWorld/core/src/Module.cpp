#include <memory>
#include <vector>

#include "module/Module.h"

#include "core/World.h"
#include "core/RegularPolygon.h"
#include "core/RectanglePolygon.h"
#include "core/TorusPolygon.h"
#include "core/YamlSerializer.h"
#include "core/tTransform.h"
#include "core/InputType.h"

#include "common/MaterialRegistry.h"


extern "C"
{
	using namespace bw::core;

	static World* gWorld{ nullptr };
	static Primitive* gPrimitive{ nullptr };

	int mod_create_world(float size)
	{
		try
		{
			float gridSize = size / 16.0f;
		
			if (size < 512.0f)
			{
				return 1;
			}
			if (gridSize > size)
			{
				return 1;
			}

			if (gWorld)
			{
				delete gWorld;
			}

			gWorld = new World(size, gridSize);

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_destroy_world()
	{
		try
		{
			if (gWorld)
			{
				delete gWorld;
				gWorld = nullptr;
			}

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_world_name(char const* name)
	{
		try
		{
			if (!gWorld)
			{
				return 1;
			}

			gWorld->setName(name);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_serialize_world(char const* filename)
	{
		try
		{
			if (!gWorld)
			{
				return 1;
			}

			auto ser = std::shared_ptr<YamlSerializer>(YamlSerializer::toFile(filename));
			auto workData = SerializationWorkData{};

			gWorld->serialize(ser, workData);
			ser->serialize();

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	void _set_primitive_material_default(uint32_t materialIndex, bw::core::MaterialDefinitionData* materialDefinition)
	{
		auto const& material = bw::common::MaterialNames[materialIndex];
		auto numParams = (uint32_t)get<1>(material);

		for (uint32_t i = 0; i < numParams; ++i)
		{
			materialDefinition->params[i] = get<3>(bw::common::MaterialParams[materialIndex][i]);
		}

		auto const& defaultColour = get<2>(bw::common::MaterialNames[materialIndex]);

		for (uint32_t i = 0; i < 3; ++i)
		{
			materialDefinition->baseColour[i] = defaultColour[i];
		}
	}

	void _set_primitive_material_defaults(bw::core::Primitive* prim)
	{
		auto properties = prim->getProperties();

		_set_primitive_material_default(properties.floorMaterialIndex, &properties.floorMaterialDef.data);
		_set_primitive_material_default(properties.ceilingMaterialIndex, &properties.ceilingMaterialDef.data);
		_set_primitive_material_default(properties.wallMaterialIndex, &properties.wallMaterialDef.data);

		prim->setProperties(properties);
	}

	void _set_primitive_materials(Primitive* primitive, uint32_t materialIndex)
	{
		auto props = primitive->getProperties();

		props.floorMaterialIndex = materialIndex;
		props.ceilingMaterialIndex = materialIndex;
		props.wallMaterialIndex = materialIndex;

		primitive->setProperties(props);

		_set_primitive_material_defaults(primitive);
	}

	int mod_create_regular_polygon(uint32_t operation, uint32_t fillType, uint32_t numSides, uint32_t materialIndex)
	{
		try
		{
			if (!gWorld)
			{
				return 1;
			}

			gPrimitive = new RegularPolygon((Primitive::Operation)operation, (Primitive::FillRule)fillType, numSides);

			_set_primitive_materials(gPrimitive, materialIndex);

			gWorld->addPrimitive(gPrimitive);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_create_rectangle_polygon(uint32_t operation, uint32_t fillType, float xyRatio, uint32_t materialIndex)
	{
		try
		{
			if (!gWorld)
			{
				return 1;
			}

			gPrimitive = new RectanglePolygon((Primitive::Operation)operation, (Primitive::FillRule)fillType, xyRatio);

			_set_primitive_materials(gPrimitive, materialIndex);

			gWorld->addPrimitive(gPrimitive);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_create_torus_polygon(uint32_t operation, uint32_t fillType, float thickness, float resolution, uint32_t materialIndex)
	{
		try
		{
			if (!gWorld)
			{
				return 1;
			}

			gPrimitive = new TorusPolygon((Primitive::Operation)operation, (Primitive::FillRule)fillType, thickness, resolution);

			_set_primitive_materials(gPrimitive, materialIndex);

			gWorld->addPrimitive(gPrimitive);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_size(float width, float height)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setSize(wp::Vector2(width, height));
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}

	}

	int mod_set_primitive_layer(uint8_t layer)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setLayer(layer);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_priority(uint8_t priority)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setPriority(priority);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_floor_z(float z)
	{
		auto properties = gPrimitive->getProperties();

		properties.floorZ = z;

		gPrimitive->setProperties(properties);
		return 1;
	}

	int mod_set_primitive_ceiling_z(float z)
	{
		auto properties = gPrimitive->getProperties();

		properties.ceilingZ = z;

		gPrimitive->setProperties(properties);
		return 1;
	}

	int mod_set_primitive_flags(uint32_t flags)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setFlags(flags);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_add_primitive_flags(uint32_t flags)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			auto curFlags = gPrimitive->getFlags();
			gPrimitive->setFlags(curFlags | flags);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_remove_primitive_flags(uint32_t flags)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			auto curFlags = gPrimitive->getFlags();
			gPrimitive->setFlags(curFlags & ~flags);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_time_update_distance(float distance)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setTimeUpdateDistance(distance);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_position(float x, float y)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setPosition(wp::Vector2(x, y));
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_transform_offset(float x, float y)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setTransformOffset(wp::Vector2(x, y));
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_influence_eye_origin_offset(float x, float y)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setInfluenceEyeOriginOffset(wp::Vector2(x, y));
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_influence_eye_angle_offset(float angle)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setInfluenceEyeAngleOffset(angle);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_follow_orbit_angle(bool follow)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			gPrimitive->setFollowOrbitAngle(follow);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	static int check_primitive_transform(uint32_t key, uint32_t index)
	{
		if (key >= (uint32_t)VertexTransformer::Key::COUNT || index >= 2)
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}

	static int get_num_transforms(VertexTransformer::Key key)
	{
		switch (key)
		{
		case VertexTransformer::Key::Scale:
			return (int)gPrimitive->getScaleTransforms().size();

		case VertexTransformer::Key::Angle:
			return (int)gPrimitive->getAngleTransforms().size();
			break;

		case VertexTransformer::Key::OrbitAngle:
			return (int)gPrimitive->getOrbitAngleTransforms().size();

		case VertexTransformer::Key::OrbitDistance:
			return (uint32_t)gPrimitive->getOrbitDistanceTransforms().size();

		default:
			return -1;
		}
	}

	int mod_set_primitive_transform_0_input(uint32_t key, uint32_t index, uint32_t inputType)
	{
		try
		{
			if (!gPrimitive || check_primitive_transform(key, index) != 0)
			{
				return 1;
			}

			if (inputType >= (uint32_t)InputType::COUNT)
			{
				return 1;
			}

			gPrimitive->setTransformOperand((VertexTransformer::Key)key, 0, index, tTransform::OperandType::Input);
			gPrimitive->setTransformInput((VertexTransformer::Key)key, 0, index, (InputType)inputType);

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_transform_0_constant(uint32_t key, uint32_t index, float value)
	{
		try
		{
			if (!gPrimitive || check_primitive_transform(key, index) != 0)
			{
				return 1;
			}

			gPrimitive->setTransformOperand((VertexTransformer::Key)key, 0, index, tTransform::OperandType::Constant);
			gPrimitive->setTransformConstant((VertexTransformer::Key)key, 0, index, value);

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_transform_0_previous(uint32_t key, uint32_t index)
	{
		try
		{
			if (!gPrimitive || check_primitive_transform(key, index) != 0)
			{
				return 1;
			}

			gPrimitive->setTransformOperand((VertexTransformer::Key)key, 0, index, tTransform::OperandType::TransformOutput);

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_transform_0_function(uint32_t key, uint32_t index, uint32_t fn, float value)
	{
		try
		{
			if (!gPrimitive || check_primitive_transform(key, index) != 0)
			{
				return 1;
			}

			if (fn < (uint32_t)tTransform::OperandType::Sine || fn > (uint32_t)tTransform::OperandType::Square)
			{
				return 1;
			}

			gPrimitive->setTransformOperand((VertexTransformer::Key)key, 0, index, (tTransform::OperandType)fn);
			gPrimitive->setTransformFnMultiplier((VertexTransformer::Key)key, 0, index, value);

			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_transform_0_operation(uint32_t key, uint32_t op)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			if (key >= (uint32_t)VertexTransformer::Key::COUNT)
			{
				return 1;
			}

			if (op >= (uint32_t)tTransform::Operation::COUNT)
			{
				return 1;
			}

			gPrimitive->setTransformOperation((VertexTransformer::Key)key, 0, (tTransform::Operation)op);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}

	int mod_set_primitive_animation_value(uint32_t key, float const* values, int numValues)
	{
		try
		{
			if (!gPrimitive)
			{
				return 1;
			}

			if (numValues % 2)
			{
				return 1;
			}

			if (key >= (uint32_t)VertexTransformer::Key::COUNT)
			{
				return 1;
			}

			std::vector<std::pair<float, float>> animValues(numValues / 2);

			for (int i = 0; i < numValues; i += 2)
			{
				animValues[i / 2] = std::make_pair(values[i], values[i + 1]);
			}

			gPrimitive->setAnimationValues((VertexTransformer::Key)key, animValues);
			return 0;
		}
		catch (std::exception& e)
		{
			printf("ERROR: %s\n", e.what());
			return 1;
		}
	}
};