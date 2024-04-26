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

#include "granular_synth.h"
#include "midi.h"

#include <mmeapi.h>

#define SAMPLE_RATE (44100)

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
			granular_synth_render_channel(&synth, channel, &outputs[channel][sample]);
		}
		granular_synth_advance(&synth, sample_rate);
	}
}

double pixel_pos_to_sample_pos(int pixelPos, int maxPixels, const smol_audiobuffer_t* buffer) {
	double relativePos = (double)pixelPos / (maxPixels - 1);
	return relativePos * ((double)buffer->num_frames / buffer->sample_rate);
}

int sample_pos_to_pixel_pos(double samplePosSec, int maxPixels, const smol_audiobuffer_t* buffer) {
	double relativePos = samplePosSec / ((double)buffer->num_frames / buffer->sample_rate);
	return (int)((double)(maxPixels - 1) * relativePos);
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

			float sample = smol_audiobuffer_sample_linear(
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
	int xPosOffset = grain->position / samplesPerPixel;

	xPosOffset += grainSamplePos / samplesPerPixel;

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_WHITE);
	smol_canvas_draw_line(canvas, bounds.x + xPosOffset, midY - halfH, bounds.x + xPosOffset, midY + halfH);
	smol_canvas_fill_circle(canvas, bounds.x + xPosOffset, midY - halfH, 3);
	smol_canvas_fill_circle(canvas, bounds.x + xPosOffset, midY + halfH, 3);
	smol_canvas_pop_color(canvas);
}

void draw_guide(smol_canvas_t* canvas, const char* text, const smol_audiobuffer_t* buffer, float samplePosSec, rect_t parentBounds) {
	const smol_u32 samplesPerPixel = buffer->num_frames / parentBounds.width;

	int xPosOffset = samplePosSec * buffer->sample_rate / samplesPerPixel;

	int w, h;
	smol_text_size(canvas, 1, text, &w, &h);

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_CYAN);
	smol_canvas_draw_line(canvas, parentBounds.x + xPosOffset, parentBounds.y, parentBounds.x + xPosOffset, parentBounds.y + parentBounds.height);
	
	int inverter = parentBounds.x + xPosOffset + w + 4 > parentBounds.width ? -w : 0;
	smol_canvas_fill_rect(canvas, parentBounds.x + xPosOffset - 2 + inverter, parentBounds.y - 2, w + 4, h + 4);
	smol_canvas_set_color(canvas, SMOLC_BLACK);
	smol_canvas_draw_text(canvas, parentBounds.x + xPosOffset + inverter, parentBounds.y, 1, text);
	
	smol_canvas_pop_color(canvas);
}

void draw_curve(smol_canvas_t* canvas, curve_t* curve, rect_t bounds) {
	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_WHITE);

	const int h = bounds.height;

	float lastValue = curve_get_value(curve, 0.0f);
	for (int ox = 1; ox < bounds.width; ox+=2) {
		float t = (float)ox / bounds.width;
		float value = curve_get_value(curve, t);

		int x0 = bounds.x + ox - 1;
		int x1 = bounds.x + ox;

		int y0 = bounds.y + lastValue * h;
		int y1 = bounds.y + value * h;

		smol_canvas_draw_line(canvas, x0, y0, x1, y1);

		lastValue = value;
	}

	smol_canvas_pop_color(canvas);
}

float pitch_from_midi(int note) {
	return powf(2.0f, (note - 69) / 12.0f);
}

void midi_callback(midi_message_t msg) {
	printf("MIDI: %d %d %d\n", msg.status, msg.data1, msg.data2);
	if (msg.status == MIDI_NOTE_ON) {
		granular_synth_noteon(
			&synth,
			msg.note.pitch,
			pitch_from_midi(msg.note.pitch),
			(float)msg.note.velocity / 127.0f
		);
	} else if (msg.status == MIDI_NOTE_OFF) {
		granular_synth_noteoff(&synth, msg.note.pitch);
	}
}

int main() {
	midi_in_device_t* midi = NULL;

	if (!smol_file_exists("config.txt")) {
		printf("Available audio devices:\n");
		int deviceCount;
		smol_audio_device_t* devices = smol_enumerate_audio_devices(0, &deviceCount);
		for (int i = 0; i < deviceCount; i++) {
			printf("%d) %s\n", i, devices[i].name);
		}
		printf("Select a device: ");

		int audio_device = -1;
		scanf("%d", &audio_device);

		smol_audio_playback_init(SAMPLE_RATE, 2, audio_device);
		smol_audio_set_playback_callback(audio_callback, NULL);

		// midi
		int midi_devices = midi_get_device_count();
		printf("Available MIDI devices:\n");
		for (int i = 0; i < midi_devices; i++) {
			char name[32];
			midi_get_device_name(i, name);
			printf("%d) %s\n", i, name);
		}
		printf("Select a device: ");

		int midi_device = -1;
		scanf("%d", &midi_device);

		midi_in_device_t* midi = midi_open_device(midi_device, midi_callback);
		if (!midi) {
			fprintf(stderr, "Failed to open MIDI device\n");
			return 1;
		}

		FILE* fp = fopen("config.txt", "w");
		fprintf(fp, "%d %d", audio_device, midi_device);
		fclose(fp);
	} else {
		// audio device index (first byte)
		int audio_device = -1;
		// midi device index (second byte)
		int midi_device = 0;
		
		FILE* fp = fopen("config.txt", "r");
		fscanf(fp, "%d %d", &audio_device, &midi_device);
		fclose(fp);

		Sleep(5000);

		smol_audio_playback_init(SAMPLE_RATE, 2, audio_device);
		smol_audio_set_playback_callback(audio_callback, NULL);

		Sleep(1000);

		midi_open_device(midi_device, midi_callback);
		if (!midi) {
			fprintf(stderr, "Failed to open MIDI device\n");
			return 1;
		}
	}

	granular_synth_init(&synth, "pad2.wav");
	synth.sample.window_start = 0.1;
	synth.sample.window_end = 1.0;
	synth.grain_settings.grains_per_second = 2;

	smol_frame_config_t frameConf;
	memset(&frameConf, 0, sizeof(smol_frame_config_t));

	frameConf.flags = SMOL_FRAME_CONFIG_HAS_TITLEBAR | SMOL_FRAME_CONFIG_OWNS_EVENT_QUEUE;
	frameConf.width = 800;
	frameConf.height = 600;
	frameConf.title = "Granular Synth";

	smol_frame_t* frame = smol_frame_create_advanced(&frameConf);
	smol_canvas_t canvas = smol_canvas_create(frameConf.width, frameConf.height);

	gui_t gui; gui_init(&gui, &canvas);

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

		rectcut_t root = { 0, 0, frame->width, frame->height };
		rectcut_expand(&root, -5);

		rectcut_t top = rectcut_top(&root, 30);

		root.miny += 5;

		rectcut_t leftRightCh = root;
		rectcut_t leftCh = rectcut_top(&root, (root.maxy - root.miny) / 2);
		rectcut_t rightCh = root;

		rect_t rectLeftCh = torect(leftCh);
		rect_t rectRightCh = torect(rightCh);
		rect_t rectLeftRightCh = torect(leftRightCh);

		smol_canvas_clear(&canvas, SMOLC_DARKEST_GREY);

		draw_waveform(&canvas, rectLeftCh, &synth.sample.buffer, 0);
		draw_waveform(&canvas, rectRightCh, &synth.sample.buffer, 1);

		int voice_count = granular_synth_active_voice_count(&synth);
		smol_u32 samplerPerPixel = synth.sample.buffer.num_frames / rectLeftRightCh.width;
		for (int id = 0; id < voice_count; id++) {
			draw_grain(&canvas, &synth.voices[id], &synth.sample.buffer, rectLeftRightCh);
		}

		draw_guide(&canvas, "LS", &synth.sample.buffer, synth.sample.window_start, rectLeftRightCh);
		draw_guide(&canvas, "LE", &synth.sample.buffer, synth.sample.window_end, rectLeftRightCh);

		//draw_curve(&canvas, &curve_test, torect(root));

		gui_begin(&gui);

		static double startTime = 0.1;
		static double endTime = 0.4;

		double maxTime = (double)(synth.sample.buffer.num_frames - 1) / synth.sample.buffer.sample_rate;

		rectcut_t sampleEndRect = rectcut_right(&top, 150);
		if (gui_spinnerd(&gui, "sampleEnd", torect(sampleEndRect), &endTime, 0.0, maxTime, 0.05, "end: %.2fs")) {
			synth.sample.window_end = endTime;
			if (synth.sample.window_end <= synth.sample.window_start) {
				synth.sample.window_end = synth.sample.window_start + 0.001;
			}
		}

		rectcut_t sampleStartRect = rectcut_right(&top, 150);
		if (gui_spinnerd(&gui, "sampleStart", torect(sampleStartRect), &startTime, 0.0, endTime, 0.05, "start: %.2fs")) {
			synth.sample.window_start = startTime;
			if (synth.sample.window_start >= synth.sample.window_end) {
				synth.sample.window_start = synth.sample.window_end - 0.001;
				synth.sample.window_start = synth.sample.window_end - 0.001;
			}
		}

		rect_t endRect = rect(
			sample_pos_to_pixel_pos(synth.sample.window_end, rectLeftRightCh.width, &synth.sample.buffer) - 10,
			rectLeftRightCh.y,
			20,
			rectLeftRightCh.height
		);
		if (gui_draggable_area(&gui, "dragEnd", GUI_DRAG_AXIS_X, endRect, &synth.sample.window_end, NULL, synth.sample.buffer.num_frames / rectLeftRightCh.width, 0.0)) {
			synth.sample.window_end = min(max(synth.sample.window_end, 0.0), synth.sample.buffer.duration);
			endTime = synth.sample.window_end;
		}

		rect_t startRect = rect(
			sample_pos_to_pixel_pos(synth.sample.window_start, rectLeftRightCh.width, &synth.sample.buffer) - 10,
			rectLeftRightCh.y,
			20,
			rectLeftRightCh.height
		);
		if (gui_draggable_area(&gui, "dragStart", GUI_DRAG_AXIS_X, startRect, &synth.sample.window_start, NULL, synth.sample.buffer.num_frames / rectLeftRightCh.width, 0.0)) {
			synth.sample.window_start = min(max(synth.sample.window_start, 0.0), synth.sample.window_end - 0.001);
			startTime = synth.sample.window_start;
		}

		gui_end(&gui);

		smol_canvas_present(&canvas, frame);
	}
	
	if (midi) {
		midi_close_device(midi);
	}

	smol_audio_shutdown();
	smol_frame_destroy(frame);

	return 0;
}
