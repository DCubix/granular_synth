#ifndef GRANULAR_SYNTH_H
#define GRANULAR_SYNTH_H

#include <stdint.h>
#include "smol_utils.h"
#include "smol_audio.h"

#define GS_ENVELOPE_MAX_POINTS 64
#define GS_ENVELOPE_MAX_SLOPES (GS_ENVELOPE_MAX_POINTS / 2)

#define GS_VOICE_MAX_GRAINS 32
#define GS_SYNTH_MAX_VOICES 8

typedef struct curve_point_t {
	float value, slope;
	double time;
} curve_point_t;

typedef smol_vector(curve_point_t) curve_points_t;
typedef smol_vector(float) float_vector_t;

typedef struct curve_t {
	curve_points_t points;
} curve_t;

void curve_init(curve_t* curve);
float curve_get_value(curve_t* curve, float ntime);
void curve_add_point(curve_t* curve, float value, double time, float slope);
void curve_set_point(curve_t* curve, size_t index, float value, double time, float slope);
void curve_free(curve_t* curve);

typedef struct adsr_t {
	float attack;
	float decay;
	float sustain;
	float release;
	float time, value;

	enum {
		ADSR_IDLE = 0,
		ADSR_ATTACK,
		ADSR_DECAY,
		ADSR_SUSTAIN,
		ADSR_RELEASE
	} state;
} adsr_t;

void adsr_init(adsr_t* adsr);
void adsr_gate(adsr_t* adsr, int gate);
void adsr_update(adsr_t* adsr, float sample_rate);

typedef struct grain_t {
	double size; // grain size in seconds
	double position; // position in seconds

	float pitch; // pitch factor, 1.0 = original pitch
	float velocity; // velocity factor (note velocity), 1.0 = original velocity

	float time;

	float computed_amplitude;
	float computed_pitch;

	curve_t amplitude_envelope;

	enum {
		GRAIN_FORWARD = 0,
		GRAIN_REVERSE
	} direction;

	enum {
		GRAIN_IDLE = 0,
		GRAIN_PREPARE,
		GRAIN_PLAYING,
		GRAIN_NOTEOFF,
		GRAIN_FINISHED
	} state;
} grain_t;

void grain_init(grain_t* grain);
void grain_set_smoothness(grain_t* grain, float smoothness);
void grain_update(grain_t* grain);
void grain_advance(grain_t* grain, float sample_rate);
void grain_noteoff(grain_t* grain);
void grain_free(grain_t* grain);

typedef struct voice_t {
	uint32_t id;

	grain_t grains[GS_VOICE_MAX_GRAINS];
	adsr_t amplitude_envelope;

	struct {
		int grains_per_second; // grains per second, min 1
		float grain_size; // grain size in seconds
		float grain_smoothness;
	} grain_settings;

	struct {
		float pitch;
		float velocity;
		float position;
	} note_settings;

	float grain_spawn_timer;

	enum {
		VOICE_IDLE = 0,
		VOICE_GATE
	} state;
} voice_t;

void voice_init(voice_t* voice);
grain_t* voice_get_free_grain(voice_t* voice);
void voice_spawn_grain(voice_t* voice);
int voice_is_free(voice_t* voice);
void voice_gate(voice_t* voice, int gate);

void voice_render_channel(voice_t* voice, smol_audiobuffer_t* buffer, int channel, float* out);
void voice_advance(voice_t* voice, float sample_rate);

typedef struct granular_synth_t {
	voice_t voices[GS_SYNTH_MAX_VOICES];

	struct {
		smol_audiobuffer_t buffer;
		double window_start, window_end; // window start and end in seconds
	} sample;

	struct {
		int grains_per_second; // grains per second, min 1
		float grain_smoothness;
	} grain_settings;
} granular_synth_t;

void granular_synth_init(granular_synth_t* synth, const char* sample_file);
void granular_synth_render_channel(granular_synth_t* synth, int channel, float* out);
void granular_synth_advance(granular_synth_t* synth, float sample_rate);

voice_t* granular_synth_get_free_voice(granular_synth_t* synth);

void granular_synth_noteon(granular_synth_t* synth, uint32_t id, float pitch, float velocity);
void granular_synth_noteoff(granular_synth_t* synth, uint32_t id);

int granular_synth_active_voice_count(granular_synth_t* synth);

#endif // !GRANULAR_SYNTH_H
