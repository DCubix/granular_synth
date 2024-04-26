#include "granular_synth.h"

#include <math.h>

float tunable_sigmoid_curve(float x, float k) {
	k = fmaxf(-1.0f, fminf(1.0f, k));
	return (x - k * x) / (k - 2.0f * k * fabsf(x) + 1.0f);
}

void curve_init(curve_t* curve) {
	smol_vector_init(&curve->points, GS_ENVELOPE_MAX_POINTS);
}

float curve_get_value(curve_t* curve, float ntime) {
	const size_t count = smol_vector_count(&curve->points);
	
	if (count == 0) {
		return 1.0f;
	}

	const float firstTime = smol_vector_at(&curve->points, 0).time;
	const float lastTime = smol_vector_at(&curve->points, count - 1).time;

	if (ntime < firstTime) {
		return smol_vector_at(&curve->points, 0).value;
	} else if (ntime >= lastTime) {
		return smol_vector_at(&curve->points, count - 1).value;
	}

	for (size_t i = 0; i < count - 1; i++) {
		curve_point_t a = smol_vector_at(&curve->points, i);
		curve_point_t b = smol_vector_at(&curve->points, i + 1);
		if (ntime >= a.time && ntime < b.time) {
			float t = (ntime - a.time) / (b.time - a.time);
			float s = a.slope;
			return a.value + tunable_sigmoid_curve(t, s) * (b.value - a.value);
		}
	}

	return 0.0f;
}

static int compare_points(const void* a, const void* b) {
	const curve_point_t* pointA = (const curve_point_t*)a;
	const curve_point_t* pointB = (const curve_point_t*)b;
	return pointA->time < pointB->time ? -1 : 1;
}

void curve_add_point(curve_t* curve, float value, double time, float slope) {
	const size_t count = smol_vector_count(&curve->points);

	if (count >= GS_ENVELOPE_MAX_POINTS) {
		return;
	}

	curve_point_t point = { value, slope, time };
	smol_vector_push(&curve->points, point);

	// sort
	smol_sort_vector(&curve->points, compare_points);
}

void curve_set_point(curve_t* curve, size_t index, float value, double time, float slope) {
	if (index >= smol_vector_count(&curve->points)) {
		return;
	}

	curve_point_t* point = &smol_vector_at(&curve->points, index);
	point->value = value;
	point->time = time;
	point->slope = slope;

	// sort
	smol_sort_vector(&curve->points, compare_points);
}

void curve_free(curve_t* curve) {
	smol_vector_free(&curve->points);
}

void adsr_init(adsr_t* adsr) {
	adsr->attack = 0.0f;
	adsr->decay = 0.0f;
	adsr->sustain = 1.0f;
	adsr->release = 0.0f;
	adsr->time = 0.0f;
	adsr->value = 0.0f;
	adsr->state = ADSR_IDLE;
}

void adsr_gate(adsr_t* adsr, int gate) {
	if (gate) {
		adsr->state = ADSR_ATTACK;
		adsr->time = 0.0f;
		adsr->value = 0.0f;
	} else {
		adsr->state = ADSR_RELEASE;
	}
}

void adsr_update(adsr_t* adsr, float sample_rate) {
	const double inv_sample_rate = 1.0 / sample_rate;
	const float k = 0.5f;
#define T tunable_sigmoid_curve(adsr->time, k)
	switch (adsr->state) {
		case ADSR_ATTACK: {
			adsr->time += inv_sample_rate / adsr->attack;
			adsr->value = smol_mixf(0.0f, 1.0f, T);
			if (adsr->time >= 1.0f) {
				adsr->state = ADSR_DECAY;
				adsr->time = 0.0f;
			}
		} break;
		case ADSR_DECAY: {
			adsr->time += inv_sample_rate / adsr->decay;
			adsr->value = smol_mixf(1.0f, adsr->sustain, T);
			if (adsr->time >= 1.0f) {
				adsr->state = ADSR_SUSTAIN;
			}
		} break;
		case ADSR_SUSTAIN: {
			adsr->value = adsr->sustain;
		} break;
		case ADSR_RELEASE: {
			adsr->time += inv_sample_rate / adsr->release;
			adsr->value = smol_mixf(adsr->sustain, 0.0f, T);
			if (adsr->time >= 1.0f) {
				adsr->state = ADSR_IDLE;
			}
		} break;
	}
#undef T
}

void grain_init(grain_t* grain) {
	grain->size = 100;
	grain->position = 0;
	grain->pitch = 1.0f;
	grain->velocity = 1.0f;
	grain->time = 0.0f;
	grain->direction = GRAIN_FORWARD;
	grain->state = GRAIN_IDLE;

	curve_init(&grain->amplitude_envelope);
	curve_add_point(&grain->amplitude_envelope, 0.0f, 0.0, 0.0);
	curve_add_point(&grain->amplitude_envelope, 1.0f, 0.001, 0.0);
	curve_add_point(&grain->amplitude_envelope, 1.0f, 0.999, 0.0);
	curve_add_point(&grain->amplitude_envelope, 0.0f, 1.0, 0.0);
	grain_set_smoothness(grain, 1.0f);
}

void grain_set_smoothness(grain_t* grain, float smoothness) {
	double in = smoothness * 0.5 + 0.001;
	double out = 1.0 - in;
	float k = (1.0f - smoothness) * 2.0f - 1.0f;
	curve_set_point(&grain->amplitude_envelope, 1, 1.0f,  in, k);
	curve_set_point(&grain->amplitude_envelope, 2, 1.0f, out, k);
}

void grain_update(grain_t* grain) {
	switch (grain->state) {
		case GRAIN_PREPARE: {
			grain->time = 0.0f;
			grain->state = GRAIN_PLAYING;
		} break;
		case GRAIN_NOTEOFF:
		case GRAIN_PLAYING: {
			grain->computed_amplitude = grain->velocity;
			grain->computed_pitch = grain->pitch;

			float t = grain->time / grain->size;

			grain->computed_amplitude *= curve_get_value(&grain->amplitude_envelope, t);

			if (t >= 1.0f) {
				grain->state = GRAIN_FINISHED;
			}
		} break;
		case GRAIN_FINISHED: return;
		default: break;
	}
}

void grain_advance(grain_t* grain, float sample_rate) {
	const double inv_sample_rate = 1.0 / sample_rate;
	grain->time += inv_sample_rate * grain->computed_pitch;
}

void grain_noteoff(grain_t* grain) {
	grain->state = GRAIN_NOTEOFF;
}

void grain_free(grain_t* grain) {
	curve_free(&grain->amplitude_envelope);
}

void voice_init(voice_t* voice) {
	voice->grain_settings.grains_per_second = 10;
	voice->grain_settings.grain_size = 0.1f;
	voice->grain_settings.grain_smoothness = 0.5f;
	voice->note_settings.pitch = 1.0f;
	voice->note_settings.velocity = 1.0f;
	voice->note_settings.position = 0.0f;
	voice->grain_spawn_timer = 0.0f;
	voice->state = VOICE_IDLE;

	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_init(&voice->grains[i]);
	}
	
	adsr_init(&voice->amplitude_envelope);
	voice->amplitude_envelope.attack = 0.1f;
	voice->amplitude_envelope.decay = 0.1f;
	voice->amplitude_envelope.sustain = 0.5f;
	voice->amplitude_envelope.release = 2.5f;
}

grain_t* voice_get_free_grain(voice_t* voice) {
	grain_t* grain = NULL;
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		if (voice->grains[i].state == GRAIN_IDLE) {
			grain = &voice->grains[i];
			break;
		}
	}
	return grain;
}

void voice_spawn_grain(voice_t* voice) {
	grain_t* grain = voice_get_free_grain(voice);
	if (grain == NULL) {
		return;
	}

	grain_init(grain);
	grain->position = voice->note_settings.position; // start position in the buffer
	grain->size = voice->grain_settings.grain_size;
	grain->pitch = voice->note_settings.pitch;
	grain->velocity = voice->note_settings.velocity;
	grain->direction = GRAIN_FORWARD; // TODO: direction parameter
	grain->state = GRAIN_PREPARE;
}

int voice_is_free(voice_t* voice) {
	return voice->amplitude_envelope.state == ADSR_IDLE &&
		voice->state == VOICE_IDLE;
}

void voice_gate(voice_t* voice, int gate) {
	adsr_gate(&voice->amplitude_envelope, gate);
	if (gate) {
		voice->state = VOICE_GATE;
	} else {
		voice->state = VOICE_IDLE;
	}
}

void voice_render_channel(voice_t* voice, smol_audiobuffer_t* buffer, int channel, float* out) {
	int all_finished = 1;
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* grain = &voice->grains[i];
		if (grain->state == GRAIN_IDLE) {
			continue;
		}

		all_finished = 0;
		grain_update(grain);

		*out += smol_audiobuffer_sample_cubic(
			buffer,
			channel,
			grain->time + grain->position
		) * grain->computed_amplitude;
	}

	if (all_finished) {
		*out = 0.0f;
	}
}

void voice_advance(voice_t* voice, float sample_rate) {
	const double inv_sample_rate = 1.0 / sample_rate;
	if (voice->state == VOICE_GATE) {
		voice->grain_spawn_timer += inv_sample_rate;
		if (voice->grain_spawn_timer >= 1.0f / voice->grain_settings.grains_per_second) {
			voice->grain_spawn_timer = 0.0f;
			voice_spawn_grain(voice);
		}
	}

	adsr_update(&voice->amplitude_envelope, sample_rate);
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* grain = &voice->grains[i];
		if (grain->state == GRAIN_FINISHED) {
			grain->state = GRAIN_IDLE;
			continue;
		}

		if (grain->state == GRAIN_IDLE) {
			continue;
		}
		grain_advance(grain, sample_rate);
	}
}

void granular_synth_init(granular_synth_t* synth, const char* sample_file) {
	synth->sample.buffer = smol_create_audiobuffer_from_wav_file(sample_file);
	synth->sample.window_start = 0.0f;
	synth->sample.window_end = synth->sample.buffer.duration;

	synth->grain_settings.grains_per_second = 10;
	synth->grain_settings.grain_smoothness = 0.5f;

	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_init(&synth->voices[i]);
	}
}

void granular_synth_render_channel(granular_synth_t* synth, int channel, float* out) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice_is_free(voice)) continue;

		voice_render_channel(voice, &synth->sample.buffer, channel, out);
	}
}

void granular_synth_advance(granular_synth_t* synth, float sample_rate) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice_is_free(voice)) continue;

		voice_advance(voice, sample_rate);
	}
}

voice_t* granular_synth_get_free_voice(granular_synth_t* synth) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice_is_free(voice)) {
			return voice;
		}
	}
	return NULL;
}

void granular_synth_noteon(granular_synth_t* synth, uint32_t id, float pitch, float velocity) {
	voice_t* voice = granular_synth_get_free_voice(synth);
	if (voice == NULL) {
		return;
	}

	voice->id = id;
	voice->note_settings.pitch = pitch;
	voice->note_settings.velocity = velocity;
	voice->note_settings.position = synth->sample.window_start;
	voice->grain_settings.grain_size = synth->sample.window_end - synth->sample.window_start;
	voice->grain_settings.grains_per_second = synth->grain_settings.grains_per_second;
	voice->grain_settings.grain_smoothness = synth->grain_settings.grain_smoothness;

	voice_gate(voice, 1);
}

void granular_synth_noteoff(granular_synth_t* synth, uint32_t id) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice->id == id) {
			voice_gate(voice, 0);
			break;
		}
	}
}

int granular_synth_active_voice_count(granular_synth_t* synth) {
	int count = 0;
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice_is_free(voice)) continue;
		count++;
	}
	return count;
}
