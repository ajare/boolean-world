#include "core/Tiling.h"


namespace bw
{
	namespace core
	{

		using namespace std;

		Tiling::Tiling(float baseSize)
			: mBaseSize(baseSize)
		{
		}

		float Tiling::getBaseSize() const
		{
			return mBaseSize;
		}

	} // core
} // bw