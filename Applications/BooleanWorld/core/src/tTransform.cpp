#include "core/tTransform.h"


namespace bw
{
	namespace core
	{
		using namespace std;

		tTransform::tTransform()
			: operation(Operation::Mul)
		{
			for (int i = 0; i < 2; ++i)
			{
				operands[i] = OperandType::Constant;
				constants[i] = 0.0f;
				fnMultipliers[i] = 1.0f;
				indices[i] = 0;
				inputs[i] = InputType::InfluenceEyeDistance;
			}
		}

		tTransform::tTransform(
			OperandType operand0,
			OperandType operand1,
			float constant0,
			float constant1,
			float fnMul0,
			float fnMul1,
			uint32_t index0,
			uint32_t index1,
			InputType input0,
			InputType input1,
			Operation _operation)
		{
			operands[0] = operand0;
			operands[1] = operand1;
			constants[0] = constant0;
			constants[1] = constant1;
			fnMultipliers[0] = fnMul0;
			fnMultipliers[1] = fnMul1;
			indices[0] = index0;
			indices[1] = index1;
			inputs[0] = input0;
			inputs[1] = input1;
			operation = _operation;
		}

		tTransform::tTransform(tTransform const& other)
		{
			copyFrom(other);
		}

		tTransform& tTransform::operator=(tTransform const& other)
		{
			copyFrom(other);
			return *this;
		}

		void tTransform::copyFrom(tTransform const& other)
		{
			for (int i = 0; i < 2; ++i)
			{
				operands[i] = other.operands[i];
				constants[i] = other.constants[i];
				fnMultipliers[i] = other.fnMultipliers[i];
				indices[i] = other.indices[i];
				inputs[i] = other.inputs[i];
			}

			operation = other.operation;
		}

		bool tTransform::childrenModified() const
		{
			return false;
		}

		void tTransform::serializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData) const
		{
			serializer->beginMap("transform");
			{
				serializer->writeUint32("operand0", (uint32_t)operands[0]);
				serializer->writeUint32("operand1", (uint32_t)operands[1]);
				serializer->writeFloat("constant0", constants[0]);
				serializer->writeFloat("constant1", constants[1]);
				serializer->writeFloat("fnMultiplier0", fnMultipliers[0]);
				serializer->writeFloat("fnMultiplier1", fnMultipliers[1]);
				serializer->writeUint32("index0", (uint32_t)indices[0]);
				serializer->writeUint32("index1", (uint32_t)indices[1]);
				serializer->writeUint32("input0", (uint32_t)inputs[0]);
				serializer->writeUint32("input1", (uint32_t)inputs[1]);
				serializer->writeUint32("operation", (uint32_t)operation);

				serializer->endMap(); // transform
			}
		}

		bool tTransform::deserializeImpl(shared_ptr<Serializer> serializer, SerializationWorkData& workData)
		{
			OperandType _operands[2];
			float _constants[2];
			float _fnMultipliers[2];
			uint32_t _indices[2];
			InputType _inputs[2];
			Operation _operation;

			try
			{
				serializer->beginMap("transform");
				{
					_operands[0] = (OperandType)serializer->readUint32("operand0");
					_operands[1] = (OperandType)serializer->readUint32("operand1");
					_constants[0] = serializer->readFloat("constant0");
					_constants[1] = serializer->readFloat("constant1");
					_fnMultipliers[0] = serializer->readFloat("fnMultiplier0");
					_fnMultipliers[1] = serializer->readFloat("fnMultiplier1");
					_indices[0] = serializer->readUint32("index0");
					_indices[1] = serializer->readUint32("index1");
					_inputs[0] = (InputType)serializer->readUint32("input0");
					_inputs[1] = (InputType)serializer->readUint32("input1");
					_operation = (Operation)serializer->readUint32("operation");

					serializer->endMap(); // transform
				}
			}
			catch (exception& e)
			{
				addDeserializationError(e.what());
				return false;
			}

			// Commit
			for (int i = 0; i < 2; ++i)
			{
				operands[i] = _operands[i];
				constants[i] = _constants[i];
				fnMultipliers[i] = _fnMultipliers[i];
				indices[i] = _indices[i];
				inputs[i] = _inputs[i];
			}

			operation = _operation;

			return true;
		}

		tTransform tTransform::makeConstant(float value)
		{
			return tTransform(
				OperandType::Constant,
				OperandType::Constant,
				value,
				value,
				1.0f,
				1.0f,
				~0u,
				~0u,
				InputType::InfluenceEyeDistance,
				InputType::InfluenceEyeDistance,
				Operation::Mul
			);
		}

		tTransform tTransform::makeZero()
		{
			return makeConstant(0.0f);
		}

		tTransform tTransform::makeOne()
		{
			return makeConstant(1.0f);
		}

		tTransform tTransform::makePassthroughPrevious()
		{
			return tTransform(
				OperandType::TransformOutput,
				OperandType::Constant,
				0.0f,
				1.0f,
				1.0f,
				1.0f,
				~0u,
				~0u,
				InputType::InfluenceEyeDistance,
				InputType::InfluenceEyeDistance,
				Operation::Mul
			);
		}

		tTransform tTransform::makePassthroughInput(InputType inputType)
		{
			return tTransform(
				OperandType::Input,
				OperandType::Constant,
				0.0f,
				1.0f,
				1.0f,
				1.0f,
				~0u,
				~0u,
				inputType,
				InputType::InfluenceEyeDistance,
				Operation::Mul
			);
		}

	} // core
} // bw