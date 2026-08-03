#ifndef RAND_GLSL
#define RAND_GLSL

#define USE_PCG32

#ifdef USE_PCG32	/////////////////////////////////////////////

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

// https://www.pcg-random.org/
struct Rand {
	uint64_t state;
	uint64_t inc;
};

uint RandUint(inout Rand r) {
	uint64_t oldState = r.state;
	r.state = oldState * 6364136223846793005ul + r.inc;
	uint xorShifted = uint(((oldState >> 18u) ^ oldState) >> 27u);
	uint rot = uint(oldState >> 59u);
	return (xorShifted >> rot) | (xorShifted << ((-rot) & 31u));
}

Rand SeedRand(uint64_t seed, uint64_t seq) {
	Rand result;
	result.state = 0;
	result.inc = (seq << 1u) | 1u;
	RandUint(result);
	result.state += seed;
	RandUint(result);
	return result;
}

float RandFloat(inout Rand r) {
	return RandUint(r) / 4294967296.0f;
}

#else	/////////////////////////////////////////////

struct Rand {
	uint state;
};

uint RandUint16(inout Rand r) {
	r.state = r.state * 1103515245 + 12345;
	return r.state >> 16;
}

Rand SeedRand(uint seed, uint seq) {
	Rand result;
	result.state = seed * 9349 + seq;
	RandUint16(result);
	return result;
}

float RandFloat(inout Rand r) {
	return RandUint16(r) / 65536.0f;
}

#endif

#endif