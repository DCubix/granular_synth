#include <stdio.h>
#include <float.h>

#define SMOL_FRAME_IMPLEMENTATION
#include "smol_frame.h"

#define SMOL_UTILS_IMPLEMENTATION
#include "smol_utils.h"

#define SMOL_CANVAS_IMPLEMENTATION
#include "smol_canvas.h"

#define SMOL_AUDIO_IMPLEMENTATION
#include "smol_audio.h"

#include "smol_font_16x16.h"

#define GUI_IMPL
#include "gui.h"

#define SAMPLE_RATE (44100)

typedef struct envelope_t {
	float factor, value;
	double time;
	enum {
		ENV_IDLE = 0,
		ENV_PREPARE,
		ENV_ATTACK,
		ENV_DECAY
	} state;
} envelope_t;

typedef struct grain_t {
	smol_u32 size; // grain size in samples
	smol_u32 offset; // position in the sample buffer

	envelope_t envelope;

	float speed;
	double time;

	enum {
		GRAIN_FORWARD = 0,
		GRAIN_REVERSE
	} direction;

	enum {
		GRAIN_IDLE = 0,
		GRAIN_PREPARE,
		GRAIN_PLAYING,
		GRAIN_FINISHED
	} state;
} grain_t;

typedef struct granular_synth_t {
	grain_t* grains;
	smol_u32 grainCount;

	struct {
		smol_audiobuffer_t buffer;
		smol_u32 start, end; // in samples
	} sample;
} granular_synth_t;


void synth_grain_update(granular_synth_t* synth, int grainId) {
	grain_t* grain = &synth->grains[grainId];

	const double invSampleRate = (1.0f / synth->sample.buffer.sample_rate);
	const double grainSizeTime = (double)grain->size * invSampleRate;

	switch (grain->state) {
		case GRAIN_PREPARE: {
			grain->time = grain->direction == GRAIN_FORWARD ? 0.0f : grainSizeTime;
			grain->state = GRAIN_PLAYING;
		} break;
		case GRAIN_FINISHED: return;
		case GRAIN_PLAYING: {
			float multiplier = grain->direction == GRAIN_FORWARD ? 1.0f : -1.0f;
			grain->time += invSampleRate * grain->speed * multiplier;
			float t = grain->time / grainSizeTime;
			float checkT = grain->direction == GRAIN_FORWARD ? t : 1.0f - t;

			if (checkT >= 1.0f) {
				grain->state = GRAIN_FINISHED;
			}
		} break;
		default: break;
	}

	switch (grain->envelope.state) {
		case ENV_PREPARE: {
			grain->envelope.time = 0.0f;
			grain->envelope.value = 0.0f;
			grain->envelope.state = ENV_ATTACK;
		} break;
		case ENV_ATTACK: {
			float attack = grain->envelope.factor;
			grain->envelope.value = grain->envelope.time / attack;
			if (grain->envelope.time >= attack) {
				grain->envelope.time = 0.0f;
				grain->envelope.state = ENV_DECAY;
				grain->envelope.value = 1.0f;
			}
			else {
				grain->envelope.time += invSampleRate * grain->speed / grainSizeTime;
			}
		} break;
		case ENV_DECAY: {
			float attack = grain->envelope.factor;
			float decay = 1.0f - attack;

			grain->envelope.value = 1.0f - (grain->envelope.time / decay);
			if (grain->envelope.time >= decay) {
				grain->envelope.time = 0.0f;
				grain->envelope.state = ENV_IDLE;
				grain->envelope.value = 0.0f;
			}
			else {
				grain->envelope.time += invSampleRate * grain->speed / grainSizeTime;
			}
		} break;
		default: break;
	}
}

void synth_init(granular_synth_t* synth, smol_u32 grainCount, const char* sampleFile) {
	synth->sample.buffer = smol_create_audiobuffer_from_wav_file(sampleFile);
	synth->sample.start = 0;
	synth->sample.end = synth->sample.buffer.num_frames;

	synth->grainCount = grainCount;
	synth->grains = (grain_t*)malloc(sizeof(grain_t) * grainCount);
	for (int i = 0; i < grainCount; i++) {
		memset(&synth->grains[i], 0, sizeof(grain_t));
	}
}

granular_synth_t synth;

void audio_callback(
	int num_input_channels,
	int num_input_samples,
	const float** inputs,
	int num_output_channels,
	int num_output_samples,
	float** outputs,
	double sample_rate,
	double inv_sample_rate,
	void* user_data
) {
	for (int sample = 0; sample < num_output_samples; sample++) {
		for (int channel = 0; channel < num_output_channels; channel++) {
			for (int i = 0; i < synth.grainCount; i++) {
				grain_t* grain = &synth.grains[i];
				if (grain->state != GRAIN_PLAYING) continue;

				float grainOffsetTime = (float)grain->offset / synth.sample.buffer.sample_rate;

				float raw = smol_audiobuffer_sample_cubic(
					&synth.sample.buffer,
					channel,
					grain->time + grainOffsetTime
				);

				outputs[channel][sample] += raw * grain->envelope.value;
			}
		}

		// update grains
		for (int i = 0; i < synth.grainCount; i++) {
			synth_grain_update(&synth, i);
		}
	}
}

void draw_waveform(
	smol_canvas_t* canvas, rect_t bounds,
	const smol_audiobuffer_t* buffer, int channel
) {
	const int halfH = bounds.height / 2;
	const int midY = bounds.y + halfH;
	smol_u32 samplesPerPixel = buffer->num_frames / bounds.width;

	smol_u32 sampleNo = 0;
	for (int ox = 0; ox < bounds.width; ox++) {
		float sampleAvg = 0.0f;
		float sampleRMS = 0.0f;
		for (int sn = 0; sn < samplesPerPixel; sn++) {
			double time = (double)sampleNo / buffer->sample_rate;

			float sample = smol_audiobuffer_sample_cubic(
				buffer,
				channel,
				time
			);
			sampleAvg += fabsf(sample);
			sampleRMS += sample * sample;

			sampleNo++;
		}

		sampleRMS = sqrtf(sampleRMS / samplesPerPixel);
		sampleAvg = (sampleAvg * 2.0f) / samplesPerPixel;

		smol_canvas_push_color(canvas);

		smol_canvas_set_color(canvas, smol_rgba(0, 140, 40, 255));
		smol_canvas_draw_line(canvas, bounds.x + ox, midY - sampleAvg * halfH, bounds.x + ox, midY + sampleAvg * halfH);

		smol_canvas_lighten_color(canvas, 95);
		smol_canvas_draw_line(canvas, bounds.x + ox, midY - sampleRMS * halfH, bounds.x + ox, midY + sampleRMS * halfH);

		smol_canvas_pop_color(canvas);
	}

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_LIGHT_GREY);
	smol_canvas_draw_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
	smol_canvas_pop_color(canvas);
}

void draw_grain(smol_canvas_t* canvas, grain_t* grain, const smol_audiobuffer_t* buffer, rect_t bounds) {
	if (grain->state == GRAIN_IDLE || grain->state == GRAIN_FINISHED) return;

	const int halfH = bounds.height / 2;
	const int midY = bounds.y + halfH;
	const smol_u32 samplesPerPixel = buffer->num_frames / bounds.width;

	int grainSamplePos = grain->time * buffer->sample_rate;
	int xPosOffset = grain->offset / samplesPerPixel;

	xPosOffset += grainSamplePos / samplesPerPixel;

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_WHITE);
	smol_canvas_draw_line(canvas, bounds.x + xPosOffset, midY - halfH, bounds.x + xPosOffset, midY + halfH);
	smol_canvas_fill_circle(canvas, bounds.x + xPosOffset, midY - halfH, 3);
	smol_canvas_fill_circle(canvas, bounds.x + xPosOffset, midY + halfH, 3);
	smol_canvas_pop_color(canvas);
}

void draw_guide(smol_canvas_t* canvas, smol_font_t font, const char* text, const smol_audiobuffer_t* buffer, int samplePos, rect_t parentBounds) {
	const smol_u32 samplesPerPixel = buffer->num_frames / parentBounds.width;

	int xPosOffset = samplePos / samplesPerPixel;

	int w, h;
	smol_text_size(font, 1, text, &w, &h);

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_CYAN);
	smol_canvas_draw_line(canvas, parentBounds.x + xPosOffset, parentBounds.y, parentBounds.x + xPosOffset, parentBounds.y + parentBounds.height);
	
	int inverter = parentBounds.x + xPosOffset + w + 4 > parentBounds.width ? -w : 0;
	smol_canvas_fill_rect(canvas, parentBounds.x + xPosOffset - 2 + inverter, parentBounds.y - 2, w + 4, h + 4);
	smol_canvas_set_color(canvas, SMOLC_BLACK);
	smol_canvas_draw_text(canvas, parentBounds.x + xPosOffset + inverter, parentBounds.y, font, 1, text);
	
	smol_canvas_pop_color(canvas);
}

grain_t* activate_grain() {
	grain_t* grain = NULL;
	for (int i = 0; i < synth.grainCount; i++) {
		if (synth.grains[i].state == GRAIN_IDLE || synth.grains[i].state == GRAIN_FINISHED) {
			grain = &synth.grains[i];
			break;
		}
	}

	if (grain) {
		grain->state = GRAIN_PREPARE;
		grain->envelope.state = ENV_PREPARE;
	}

	return grain;
}

void place_grain() {
	grain_t* g = activate_grain();
	if (g) {
		int maxSize = synth.sample.end - synth.sample.start;
		g->size = smol_rnd(4410, maxSize - 1);
		g->offset = smol_rnd(synth.sample.start, synth.sample.end - g->size);
		g->speed = smol_rndf(0.99f, 1.0f);
		g->direction = smol_rnd(0, 100) > 50 ? GRAIN_REVERSE : GRAIN_FORWARD;
		g->envelope.factor = 0.5f;
	}
}

int main() {
	synth_init(&synth, 100, "piano3.wav");

	smol_frame_config_t frameConf;
	memset(&frameConf, 0, sizeof(smol_frame_config_t));

	frameConf.flags = SMOL_FRAME_CONFIG_HAS_TITLEBAR | SMOL_FRAME_CONFIG_OWNS_EVENT_QUEUE;
	frameConf.width = 800;
	frameConf.height = 600;
	frameConf.title = "Granular Synth";

	smol_frame_t* frame = smol_frame_create_advanced(&frameConf);
	smol_canvas_t canvas = smol_canvas_create(frameConf.width, frameConf.height);

	gui_t gui; gui_init(&gui, &canvas);

	smol_audio_init(SAMPLE_RATE, 2);
	smol_audio_set_callback(audio_callback, NULL);

	smol_font_t font = smol_create_font(
		(char*)PXF_SMOL_FONT_16X16_DATA,
		PXF_SMOL_FONT_16X16_WIDTH,
		PXF_SMOL_FONT_16X16_HEIGHT,
		(smol_font_hor_geometry_t*)PXF_SMOL_FONT_16X16_OFFSET_X_WIDTH
	);

	double clock = 0.0f;
	double loop_start = smol_timer();

	static int randomize = 0;

	while (!smol_frame_is_closed(frame)) {
		double current = smol_timer();
		double time = current - loop_start;
		loop_start = current;

		clock += time;

		smol_frame_update(frame);
		
		SMOL_FRAME_EVENT_LOOP(frame, ev) {
			if (ev.type == SMOL_FRAME_EVENT_MOUSE_BUTTON_UP) {
				gui_input_mouse_click(&gui, SMOL_FALSE);
			}
			else if (ev.type == SMOL_FRAME_EVENT_MOUSE_BUTTON_DOWN) {
				gui_input_mouse_click(&gui, SMOL_TRUE);
			}
			else if (ev.type == SMOL_FRAME_EVENT_MOUSE_MOVE) {
				gui_input_mouse_move(
					&gui,
					(point_t) { ev.mouse.x, ev.mouse.y },
					(point_t) { ev.mouse.dx, ev.mouse.dy }
				);
			}
		}

		if (clock >= 0.1f && randomize) {
			place_grain();
			clock = 0.0f;
		}

		rectcut_t root = { 0, 0, frame->width, frame->height };
		rectcut_expand(&root, -5);

		smol_canvas_clear(&canvas, SMOLC_DARKEST_GREY);

		rectcut_t top = rectcut_top(&root, 30);

		root.miny += 5;

		rectcut_t leftRightCh = root;
		rectcut_t leftCh = rectcut_top(&root, (root.maxy - root.miny) / 2);
		rectcut_t rightCh = root;

		rect_t rectLeftCh = torect(leftCh);
		rect_t rectRightCh = torect(rightCh);
		rect_t rectLeftRightCh = torect(leftRightCh);

		draw_waveform(&canvas, rectLeftCh, &synth.sample.buffer, 0);
		draw_waveform(&canvas, rectRightCh, &synth.sample.buffer, 1);

		smol_u32 samplerPerPixel = synth.sample.buffer.num_frames / rectLeftRightCh.width;
		for (int grainId = 0; grainId < synth.grainCount; grainId++) {
			draw_grain(&canvas, &synth.grains[grainId], &synth.sample.buffer, rectLeftRightCh);
		}

		draw_guide(&canvas, font, "start", &synth.sample.buffer, synth.sample.start, rectLeftRightCh);
		draw_guide(&canvas, font, "end", &synth.sample.buffer, synth.sample.end, rectLeftRightCh);

		gui_begin(&gui);

		rectcut_t btnPlaceRect = rectcut_left(&top, 120);
		if (gui_button(&gui, "btnPlace", "Place Grain", torect(btnPlaceRect))) {
			place_grain();
		}

		rectcut_t btnPlace10Rect = rectcut_left(&top, 120);
		if (gui_button(&gui, "btnPlace10", "Place 10", torect(btnPlace10Rect))) {
			for (int i = 0; i < 10; i++) {
				place_grain();
			}
		}

		rectcut_t btnRandRect = rectcut_left(&top, 120);
		gui_button_toggle(&gui, "btnRandomizer", "Randomizer", torect(btnRandRect), &randomize);

		static double startTime = 0.0;
		static double endTime = 0.0;

		double maxTime = (double)(synth.sample.buffer.num_frames - 1) / synth.sample.buffer.sample_rate;

		rectcut_t sampleEndRect = rectcut_right(&top, 150);
		if (gui_spinnerd(&gui, "sampleEnd", torect(sampleEndRect), &endTime, 0.0, maxTime, 0.05, "end: %.2fs")) {
			synth.sample.end = (int)(endTime * synth.sample.buffer.sample_rate);
			if (synth.sample.end <= synth.sample.start) {
				synth.sample.end = synth.sample.start + 1;
			}
		}

		rectcut_t sampleStartRect = rectcut_right(&top, 150);
		if (gui_spinnerd(&gui, "sampleStart", torect(sampleStartRect), &startTime, 0.0, endTime, 0.05, "start: %.2fs")) {
			synth.sample.start = (int)(startTime * synth.sample.buffer.sample_rate);
			if (synth.sample.start >= synth.sample.end) {
				synth.sample.start = synth.sample.end - 1;
			}
		}

		gui_end(&gui);

		smol_canvas_present(&canvas, frame);
	}

	smol_audio_shutdown();
	smol_frame_destroy(frame);

	return 0;
}
