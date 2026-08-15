#include "willpower/common/Exceptions.h"

#include "willpower/serialization/SerializationUtils.h"

using namespace std;

namespace WP_NAMESPACE
{
	namespace serialization
	{

		using namespace WP_NAMESPACE;

		void SerializationUtils::writeInt8(std::ostream& fp, int8_t value)
		{

			fp.write((char const*)&value, sizeof(int8_t));
		}

		int8_t SerializationUtils::readInt8(std::istream& fp)
		{
			int8_t value;
			fp.read((char*)&value, sizeof(int8_t));
			return value;
		}

		void SerializationUtils::writeUint8(std::ostream& fp, uint8_t value)
		{
			fp.write((char const*)&value, sizeof(uint8_t));
		}

		uint8_t SerializationUtils::readUint8(std::istream& fp)
		{
			uint8_t value;

			fp.read((char*)&value, sizeof(uint8_t));
			return value;
		}

		void SerializationUtils::writeInt16(ostream& fp, int16_t value)
		{
			fp.write((char const*)&value, sizeof(int16_t));
		}

		int16_t SerializationUtils::readInt16(istream& fp)
		{
			int16_t value;

			fp.read((char*)&value, sizeof(int16_t));
			return value;
		}

		void SerializationUtils::writeUint16(ostream& fp, uint16_t value)
		{
			fp.write((char const*)&value, sizeof(uint16_t));
		}

		uint16_t SerializationUtils::readUint16(istream& fp)
		{
			uint16_t value;

			fp.read((char*)&value, sizeof(uint16_t));
			return value;
		}

		void SerializationUtils::writeInt32(ostream& fp, int32_t value)
		{
			fp.write((char const*)&value, sizeof(int32_t));
		}

		int32_t SerializationUtils::readInt32(istream& fp)
		{
			int32_t value;

			fp.read((char*)&value, sizeof(int32_t));
			return value;
		}

		void SerializationUtils::writeUint32(ostream& fp, uint32_t value)
		{
			fp.write((char const*)&value, sizeof(uint32_t));
		}

		uint32_t SerializationUtils::readUint32(istream& fp)
		{
			uint32_t value;

			fp.read((char*)&value, sizeof(uint32_t));
			return value;
		}

		void SerializationUtils::writeInt64(ostream& fp, int64_t value)
		{
			fp.write((char const*)&value, sizeof(int64_t));
		}

		int64_t SerializationUtils::readInt64(istream& fp)
		{
			int64_t value;

			fp.read((char*)&value, sizeof(int64_t));
			return value;
		}

		void SerializationUtils::writeUint64(ostream& fp, uint64_t value)
		{
			fp.write((char const*)&value, sizeof(uint64_t));
		}

		uint64_t SerializationUtils::readUint64(istream& fp)
		{
			uint64_t value;

			fp.read((char*)&value, sizeof(uint64_t));
			return value;
		}

		void SerializationUtils::writeReal32(ostream& fp, float value)
		{
			fp.write((char const*)&value, sizeof(float));
		}

		float SerializationUtils::readReal32(istream& fp)
		{
			float value;

			fp.read((char*)&value, sizeof(float));
			return value;
		}

		void SerializationUtils::writeReal64(ostream& fp, double value)
		{
			fp.write((char const*)&value, sizeof(double));
		}

		double SerializationUtils::readReal64(istream& fp)
		{
			double value;

			fp.read((char*)&value, sizeof(double));
			return value;
		}

		void SerializationUtils::writeVector2(ostream& fp, Vector2 const& value)
		{
			writeReal32(fp, value.x);
			writeReal32(fp, value.y);
		}

		void SerializationUtils::writeVector2(ostream& fp, float x, float y)
		{
			writeReal32(fp, x);
			writeReal32(fp, y);
		}

		Vector2 SerializationUtils::readVector2(istream& fp)
		{
			float x = readReal32(fp);
			float y = readReal32(fp);
			return Vector2(x, y);
		}


		void SerializationUtils::writeString(ostream& fp, string const& value)
		{
			char const* valueStr = value.c_str();
			fp.write(valueStr, strlen(valueStr) + 1);
		}

		string SerializationUtils::readString(std::istream& fp, int32_t maxChars)
		{
			string value;
			int32_t charCount = 0;
			char ch;

			fp.get(ch);
			while (ch != 0)
			{
				if (maxChars == charCount)
				{
					throw Exception("read past the maximum number of string characters allowed.");
				}

				value += ch;
				fp.get(ch);
				charCount++;
			}

			return value;
		}

		void SerializationUtils::writeByteArray(std::ostream& fp, char const* buffer, uint32_t length)
		{
			writeUint32(fp, length);
			fp.write(buffer, length);
		}

		char* SerializationUtils::readByteArray(istream& fp, uint32_t& length)
		{
			length = readUint32(fp);
			
			char* buffer = new char[length];
			fp.read(buffer, length);

			return buffer;
		}

	} // serialization 
} // WP_NAMESPACE
