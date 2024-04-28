#include "granular_synth.h"

#include <math.h>

float tunable_sigmoid_curve(float x, float k) {
	k = fmaxf(-1.0f, fminf(1.0f, k));
	return (x - k * x) / (k - 2.0f * k * fabsf(x) + 1.0f);
}

void curve_init(curve_t* curve) {
	if (smol_vector_count(&curve->points) > 0) {
		smol_vector_free(&curve->points);
	}
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
		adsr->time = 0.0f;
		adsr->state = ADSR_RELEASE;
	}
}

void adsr_update(adsr_t* adsr, float sample_rate) {
	const double inv_sample_rate = 1.0 / sample_rate;
	const float k = 0.5f;
	switch (adsr->state) {
		case ADSR_IDLE: {
			adsr->value = 0.0f;
			adsr->time = 0.0f;
		} break;
		case ADSR_ATTACK: {
			float t = adsr->time / adsr->attack;

			adsr->value = smol_mixf(0.0f, 1.0f, tunable_sigmoid_curve(t, k));
			if (t >= 1.0f) {
				adsr->state = ADSR_DECAY;
				adsr->time = 0.0f;
			}
			else {
				adsr->time += inv_sample_rate;
			}
		} break;
		case ADSR_DECAY: {
			float t = adsr->time / adsr->decay;

			adsr->value = smol_mixf(1.0f, adsr->sustain, tunable_sigmoid_curve(1.0f - t, k));

			if (t >= 1.0f) {
				adsr->state = ADSR_SUSTAIN;
			}
			else {
				adsr->time += inv_sample_rate;
			}
		} break;
		case ADSR_SUSTAIN: {
			adsr->value = adsr->sustain;
		} break;
		case ADSR_RELEASE: {
			float t = adsr->time / adsr->release;
			
			adsr->value = smol_mixf(0.0f, adsr->sustain, tunable_sigmoid_curve(1.0f - t, k));
			if (t >= 1.0f) {
				adsr->state = ADSR_IDLE;
			}
			else {
				adsr->time += inv_sample_rate;
			}
		} break;
	}
}

void grain_init(grain_t* grain) {
	grain->size = 100;
	grain->position = 0;
	grain->pitch = 1.0f;
	grain->velocity = 1.0f;
	grain->time = 0.0f;
	grain->play_mode = GRAIN_FORWARD;
	grain->smoothness = 1.0f;
	grain->state = GRAIN_IDLE;
}

void grain_update(grain_t* grain, float sample_rate) {
	switch (grain->state) {
		case GRAIN_PLAYING: {
			grain->computed_amplitude = grain->velocity;
			grain->computed_pitch = grain->pitch;

			float ntime = grain->time / grain->size;
			float t = grain_get_time_factor(grain, ntime);

			// compute Attack/Decay envelope based on smoothness
			// smoothness = 0.0 -> hold, 1.0 -> smooth
			float factor = smol_clampf(grain->smoothness, 0.0f, 1.0f) * 0.5f;

			float amp = 1.0f;
			if (t <= factor) {
				float ratio = t / factor;
				ratio = tunable_sigmoid_curve(ratio, -0.5f);
				amp = smol_mixf(0.0f, 1.0f, ratio);
			}
			else if (t >= 1.0f - factor) {
				float ratio = (t - (1.0f - factor)) / factor;
				ratio = tunable_sigmoid_curve(ratio, -0.5f);
				amp = smol_mixf(0.01f, 1.0f, 1.0f - ratio);
			}

			grain->computed_amplitude *= amp;
			grain->computed_time = (double)t * grain->size;

			if (grain_check_grain_end(grain, ntime)) {
				grain->state = GRAIN_FINISHED;
				grain->computed_time = 0.0;
				grain->time = 0.0f;
			}
		} break;
		default: break;
	}
	const double inv_sample_rate = 1.0 / sample_rate;
	grain->time += inv_sample_rate * grain->computed_pitch;
}

int grain_is_free(grain_t* grain) {
	return grain->state == GRAIN_IDLE || grain->state == GRAIN_FINISHED;
}

float grain_get_time_factor(grain_t* grain, float ntime) {
	switch (grain->play_mode) {
		case GRAIN_FORWARD: {
			return ntime;
		} break;
		case GRAIN_REVERSE: {
			return 1.0f - ntime;
		} break;
		case GRAIN_PINGPONG: {
			float t = fmodf(ntime, 2.0f);
			if (t > 1.0f) {
				t = 2.0f - t;
			}
			return t;
		} break;
		default: return 0.0f;
	}
}

int grain_check_grain_end(grain_t* grain, float ntime) {
	switch (grain->play_mode) {
		case GRAIN_FORWARD:
		case GRAIN_REVERSE: return ntime >= 1.0f;
		case GRAIN_PINGPONG: return ntime >= 2.0f;
		default: return 0;
	}
}

void grain_render_channel(grain_t* grain, smol_audiobuffer_t* buffer, int channel, float* out) {
	if (grain->state != GRAIN_PLAYING) {
		*out = 0.0f;
		return;
	}

	*out = smol_audiobuffer_sample_linear(
		buffer,
		channel,
		grain->computed_time + grain->position
	) * grain->computed_amplitude;
}

void voice_init(voice_t* voice) {
	voice->grain_settings.grains_per_second = 10;
	voice->grain_settings.position = 0.0f;
	voice->grain_settings.size = 0.1f;
	voice->grain_settings.smoothness = 1.0f;
	voice->note_settings.pitch = 1.0f;
	voice->note_settings.velocity = 1.0f;
	voice->grain_spawn_timer = 0.0f;
	voice->state = VOICE_IDLE;

	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_init(&voice->grains[i]);
	}
	
	adsr_init(&voice->amplitude_envelope);
	voice->amplitude_envelope.attack = 0.4f;
	voice->amplitude_envelope.decay = 0.0f;
	voice->amplitude_envelope.sustain = 1.0f;
	voice->amplitude_envelope.release = 1.0f;
}

grain_t* voice_get_free_grain(voice_t* voice) {
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* current = &voice->grains[i];
		if (grain_is_free(current)) {
			return current;
		}
	}
	return NULL;
}

void voice_spawn_grain(voice_t* voice) {
	grain_t* grain = voice_get_free_grain(voice);
	if (grain == NULL) {
		return;
	}

	grain_init(grain);
	grain->size = voice->grain_settings.size;
	grain->position = voice->grain_settings.position; // start position in the buffer
	grain->smoothness = voice->grain_settings.smoothness;
	grain->pitch = voice->note_settings.pitch;
	grain->velocity = voice->note_settings.velocity;
	grain->play_mode = GRAIN_FORWARD; // TODO: direction parameter
	grain->state = GRAIN_PLAYING;

	// apply random size
	grain->size += smol_randf(
		-voice->random_settings.size_random,
		voice->random_settings.size_random
	);

	// apply random position offset in %
	double offset = voice->random_settings.position_offset_random * grain->size;
	grain->position += smol_randf(-offset, offset);
}

int voice_is_free(voice_t* voice) {
	return voice->amplitude_envelope.state == ADSR_IDLE;
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
	float accum = 0.0f;
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* grain = &voice->grains[i];
		if (grain_is_free(grain)) continue;

		float val = 0.0f;
		grain_render_channel(grain, buffer, channel, &val);
		accum += val;
	}

	*out = accum * voice->amplitude_envelope.value;
}

void voice_advance(voice_t* voice, float sample_rate) {
	const double inv_sample_rate = 1.0 / sample_rate;

	if (!voice_is_free(voice)) {
		voice->grain_spawn_timer += inv_sample_rate;
		if (voice->grain_spawn_timer >= 1.0f / voice->grain_settings.grains_per_second) {
			voice->grain_spawn_timer = 0.0f;
			voice_spawn_grain(voice);
		}
	}

	int all_grains_finished = 1;
	for (size_t i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* grain = &voice->grains[i];
		if (grain_is_free(grain)) continue;
		grain_update(grain, sample_rate);
		all_grains_finished = 0;
	}

	adsr_update(&voice->amplitude_envelope, sample_rate);

	if (all_grains_finished && voice->amplitude_envelope.state == ADSR_IDLE) {
		voice->state = VOICE_IDLE;
	}
}

void granular_synth_init(granular_synth_t* synth, const char* sample_file) {
	synth->sample.buffer = smol_create_audiobuffer_from_wav_file(sample_file);
	synth->sample.window_start = 0.0f;
	synth->sample.window_end = synth->sample.buffer.duration;

	synth->grain_settings.grains_per_second = 10;
	synth->grain_settings.grain_smoothness = 1.0f;

	synth->random_settings.size_random = 0.0f;
	synth->random_settings.position_offset_random = 0.0f;

	synth->tuning = 0.0f;

	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_init(&synth->voices[i]);
	}
}

void granular_synth_render_channel(granular_synth_t* synth, int channel, float* out) {
	float accum = 0.0f;
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice_is_free(voice)) continue;

		float val = 0.0f;
		voice_render_channel(voice, &synth->sample.buffer, channel, &val);
		accum += val;
	}
	*out = accum;
}

void granular_synth_advance(granular_synth_t* synth) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		voice_advance(voice, (float)synth->sample.buffer.sample_rate);
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

	voice_init(voice);
	voice->id = id;
	voice->note_settings.pitch = pitch + synth->tuning;
	voice->note_settings.velocity = velocity;
	voice->grain_settings.position = synth->sample.window_start;
	voice->grain_settings.size = synth->sample.window_end - synth->sample.window_start;
	voice->grain_settings.grains_per_second = synth->grain_settings.grains_per_second;
	voice->grain_settings.smoothness = synth->grain_settings.grain_smoothness;
	voice->grain_spawn_timer = voice->grain_settings.grains_per_second;
	voice->random_settings.size_random = synth->random_settings.size_random;
	voice->random_settings.position_offset_random = synth->random_settings.position_offset_random;
	
	voice_gate(voice, 1);
}

void granular_synth_noteoff(granular_synth_t* synth, uint32_t id) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		if (voice->id == id) {
			voice_gate(voice, 0);
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

void granular_synth_for_each_voice(granular_synth_t* synth, granular_synth_voice_cb callback, void* data) {
	for (size_t i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
		voice_t* voice = &synth->voices[i];
		callback(i, voice, data);
	}
}
