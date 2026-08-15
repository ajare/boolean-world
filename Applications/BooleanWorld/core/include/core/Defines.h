#pragma once

#include <cstdint>

// Type for storing a frame number.  This increments every frame and so needs to be large enough
// to store a long play time.  At 60FPS, 32 bits is (2^32)/(60*60*60*24) = 828 days
typedef int64_t									frame_number_type;

// Maths etc
#define BW_PI									3.14159265359
#define BW_TWOPI								(BW_PI * 2)

// World size.  This is the fixed dimension in both axes, and is centred on the origin.
#define BW_WORLD_SIZE							8192

// This must be a power of two and less than 256, as we need to use the upper bit of a
// uint8_t for storing a special flag.
#define BW_PRIMITIVE_GRID_DIM_MAX				128

// Total number of vertices.  We need to define a maximum because we pack vertex index into z-coords.  Note that
// in practise, the value is actually less, because some high indices are reserved for ghost primitives and clipping
// primitives.
#define BW_VERTEX_COUNT_MAX						(1 << 20)
#define BW_VERTEX_GHOST_ID						(BW_VERTEX_COUNT_MAX - 1)
#define BW_VERTEX_RECTCLIP_ID					(BW_VERTEX_COUNT_MAX - 2)
#define BW_VERTEX_COUNT_USEABLE_MAX				(BW_VERTEX_COUNT_MAX - 2)

// Total number of vertices per Primitive.  Indices are packed into the Vertex Z bitfield with index 0 being 
// at (2^14)
#define BW_WORLD_PRIMITIVE_VERTEX_COUNT_MAX		(1 << 10)

// Total number of Primitives in the world.  Primitive indices are packed into the Vertex Z bitfield,
// so care needs to be taken when modifying this value.
#define	BW_WORLD_PRIMITIVE_NO_INDEX				((1 << 14) - 1)
#define BW_WORLD_PRIMITIVE_COUNT_MAX			BW_WORLD_PRIMITIVE_NO_INDEX

#define BW_LAYER_MIN_VALUE						0
#define BW_LAYER_MAX_VALUE						255
#define BW_LAYER_ALL							255

#define BW_PRIORITY_MIN_VALUE					0
#define BW_PRIORITY_MAX_VALUE					255

#define BW_PRIMITIVE_INTERACTS_FLAG					0x0001          // Does this primitive participate in triangulation?
#define BW_PRIMITIVE_GHOST_FLAG						0x0002          // For the editor: indicates this should not be loaded into a game.
#define BW_PRIMITIVE_NO_TIME_UPDATE_PLAYER_STATIC	0x0004			// Don't update Primitive time if player isn't moving
#define BW_PRIMITIVE_NO_TIME_UPDATE_IF_VISIBLE		0x0008			// Don't update Primitive time if visible to player
#define BW_PRIMITIVE_EXACT_BOUNDS_FLAG				0x0010			// Calculate exact bounds using vertices

/* Z-packing macros.

Bitfield layout:

Bit       Use
---------------------------
64        <end-of-bitfield>
49        <free>
48        Next properties set
47        Prev properties set
46        Is interpolated
44        Primitive operation
24        Global vertex index     
14        Primitive vertex index
 0        Primitive index
 ---------------------------

*/ 

#define BW_BITS_CL(v, count, offset)			(v & ~(((1LL << count) - 1LL) << offset))     // clear bits [offset, offset+count)
#define BW_BITS_OR(v, value, offset)			(v | (value << offset))                       // union bits starting at offset with value
#define BW_BITS_TR(v, bits)						(v & ((1LL << bits) - 1LL))                   // trim/clip a value to bits number of bits
#define BW_BITS_OF(v, offset)					(v << offset)                                 // offset a value
#define BW_BITS_GT(v, count, offset)			((v >> offset) & ((1LL << count) - 1LL))      // get bits [offset, offset+count)
#define BW_BIT_ST(v, value, offset)				((v & ~(1LL << offset)) | ((int64_t)value << offset))  // set an individual bit at (offset)
#define BW_BIT_GT(v, offset)                    ((v >> offset) & 1LL)                         // get an individual bit at (offset)

#define BW_VERTEX_Z_PACK_PRIMITIVE_INDEX(v, i)  BW_BITS_OR(BW_BITS_CL(v, 14, 0), BW_BITS_TR(i, 14), 0)
#define BW_VERTEX_Z_UNPACK_PRIMITIVE_INDEX(v)   BW_BITS_GT(v, 14, 0)

#define BW_VERTEX_Z_PACK_PRIM_VERT_INDEX(v, i)  BW_BITS_OR(BW_BITS_CL(v, 10, 14), BW_BITS_TR(i, 10), 14)
#define BW_VERTEX_Z_UNPACK_PRIM_VERT_INDEX(v)   BW_BITS_GT(v, 10, 14)

#define BW_VERTEX_Z_PACK_VERTEX_INDEX(v, i)     BW_BITS_OR(BW_BITS_CL(v, 20, 24), BW_BITS_TR(i, 20), 24)
#define BW_VERTEX_Z_UNPACK_VERTEX_INDEX(v)      BW_BITS_GT(v, 20, 24)

#define BW_VERTEX_Z_PACK_PRIMITIVE_OP(v, i)     BW_BITS_OR(BW_BITS_CL(v, 2, 44), BW_BITS_TR(i, 2), 44)
#define BW_VERTEX_Z_UNPACK_PRIMITIVE_OP(v)      BW_BITS_GT(v, 2, 44)

#define BW_VERTEX_Z_SET_INTERPOLATED(v, i)      BW_BIT_ST(v, i, 46)			
#define BW_VERTEX_Z_IS_INTERPOLATED(v)			BW_BIT_GT(v, 46)

#define BW_VERTEX_Z_SET_PREV_PROP(v, i)	        BW_BIT_ST(v, i, 47)			
#define BW_VERTEX_Z_GET_PREV_PROP(v)            BW_BIT_GT(v, 47)

#define BW_VERTEX_Z_SET_NEXT_PROP(v, i)	        BW_BIT_ST(v, i, 48)			
#define BW_VERTEX_Z_GET_NEXT_PROP(v)            BW_BIT_GT(v, 48)

// This is the maximum size that an interpolator for distance can take.  In particular this is used for influence zones.
#define BW_INTERPOLATOR_MAX_DISTANCE			500.0f
#define BW_INTERPOLATOR_MAX_SCALE				10.0f
#define BW_INTERPOLATOR_MAX_ANGLE				360.0f

// WorldDataGenerator flags
#define BW_WDG_ALWAYS_FULL_CLIP					0x0001

// The extra padding that is added on to the Player's view distance when calculating how close a Primitive
// needs to be (taking its size into consideration), in order for the Primitive to update its Vertex positions.
#define BW_PRIMITIVE_VERTEX_CALC_DIST_PADDING	128

//
// Global events triggered by Primitive animation
//

// Debug event.
#define BW_PRIMITIVE_GLOBAL_EVENT_DEBUG			0x00000001

// Execute a clip on this frame.  Useful for when a Primitive has connected others together.
#define BW_PRIMITIVE_GLOBAL_EVENT_CLIP			0x00000002

//
// Material definitions
//
#define BW_MATERIAL_COUNT						2
#define BW_MATERIAL_PARAMS_MAX					8
