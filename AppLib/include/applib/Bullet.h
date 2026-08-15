#pragma once

#include <cstdint>

#include <willpower/common/Vector2.h>

#define BULLET_STATE_FIRING			0x01
#define BULLET_STATE_EXPLODING		0x02

#define BULLET_HIT_EXPLODE			0x01
#define BULLET_HIT_REFLECT			0x02

namespace applib
{

	struct BulletParams
	{
		float speed;
		float life{ -1.0f };
		uint32_t flags{ 0 };
	};

	struct BulletFireParams
	{
		float angleOffset{ 0.0f };
	};

	struct Bullet
	{
		int type;
		BulletParams params;

		// Physical
		wp::Vector2 p_pos;
		wp::Vector2 p_dir;
		float p_radius;

		// Visual
		float v_timer{ 0.0f };
		uint32_t v_fire_anim{ ~0u };
		uint32_t v_explode_anim{ ~0u };
		int v_frame{ 0 };
		int v_dir{ 1 };

		// State
		int s_state{ BULLET_STATE_FIRING };
		float s_time{ 0.0f };
	};

} // applib