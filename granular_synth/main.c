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

#include <SDL2/SDL.h>

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

//grain_t grain_test;
//voice_t voice_test;

void SDLCALL sdl_audio_callback(void* ud, Uint8* stream, int len) {
	float* buffer = (float*)stream;
	int num_samples = len / (sizeof(float) * 2);

	for (int sample = 0; sample < num_samples; sample++) {
		float left, right;
		granular_synth_render_channel(&synth, 0, &left);
		granular_synth_render_channel(&synth, 1, &right);

		granular_synth_advance(&synth);
		
		// [GRAIN TEST]
		//grain_render_channel(&grain_test, &synth.sample.buffer, 0, &left);
		//grain_render_channel(&grain_test, &synth.sample.buffer, 1, &right);

		//grain_update(&grain_test, synth.sample.buffer.sample_rate);

		// [VOICE TEST]
		/*voice_advance(&voice_test, synth.sample.buffer.sample_rate);

		voice_render_channel(&voice_test, &synth.sample.buffer, 0, &left);
		voice_render_channel(&voice_test, &synth.sample.buffer, 1, &right);*/

		*buffer++ = left;
		*buffer++ = right;
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
	
	smol_canvas_push_color(canvas);

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

		smol_canvas_set_color(canvas, smol_rgba(0, 140, 40, 255));
		smol_canvas_draw_line(canvas, bounds.x + ox, midY - sampleAvg * halfH, bounds.x + ox, midY + sampleAvg * halfH);

		smol_canvas_lighten_color(canvas, 95);
		smol_canvas_draw_line(canvas, bounds.x + ox, midY - sampleRMS * halfH, bounds.x + ox, midY + sampleRMS * halfH);
		
	}
	smol_canvas_pop_color(canvas);

	smol_canvas_push_color(canvas);
	smol_canvas_set_color(canvas, SMOLC_LIGHT_GREY);
	smol_canvas_draw_rect(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
	smol_canvas_pop_color(canvas);
}

void draw_grain(smol_canvas_t* canvas, voice_t* voice, grain_t* grain, const smol_audiobuffer_t* buffer, rect_t bounds) {
	if (grain->state == GRAIN_IDLE || grain->state == GRAIN_FINISHED) return;

	const smol_u32 samplesPerPixel = buffer->num_frames / bounds.width;

	int grainSamplePos = (grain->computed_time + grain->position) * buffer->sample_rate;
	int xPosOffset = grainSamplePos / samplesPerPixel;

	smol_canvas_push_color(canvas);

	smol_canvas_set_color(canvas, SMOLC_WHITE);
	smol_canvas_draw_line(canvas, bounds.x + xPosOffset, bounds.y, bounds.x + xPosOffset, bounds.y + bounds.height-1);

	int h = bounds.height * grain->computed_amplitude * voice->amplitude_envelope.value;

	smol_canvas_set_color(canvas, SMOLC_SKYBLUE);
	smol_canvas_fill_rect(canvas, bounds.x + xPosOffset - 2, bounds.y + (bounds.height - h), 4, h);

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
	// note should start at C
	note -= 3;
	return powf(2.0f, (note - 69) / 12.0f);
}

void midi_callback(midi_message_t msg) {
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

void draw_grain_info(int id, voice_t* voice, void* data) {
	smol_canvas_t* canvas = (smol_canvas_t*)data;

	// black background
	//smol_canvas_push_blend(canvas);
	smol_canvas_push_color(canvas);
	//smol_canvas_set_blend(canvas, smol_pixel_blend_mix);
	smol_canvas_set_color(canvas, smol_rgba(0, 0, 0, 120));
	smol_canvas_fill_rect(canvas, id * 100, 0, 100, 640);

	smol_canvas_set_color(canvas, SMOLC_WHITE);

	for (int i = 0; i < GS_VOICE_MAX_GRAINS; i++) {
		grain_t* grain = &voice->grains[i];

		int x = 10 + id * 100;
		int y = 10 + i * 18;

		char state = 'I';
		switch (grain->state) {
			case GRAIN_IDLE: state = 'I'; break;
			case GRAIN_FINISHED: state = 'F'; break;
			case GRAIN_PLAYING: state = '>'; break;
		}
		smol_canvas_draw_text_formated(canvas, x, y, 1, "G%d: %c%.1f", i, state, grain->time);
	}

	smol_canvas_pop_color(canvas);
	//smol_canvas_pop_blend(canvas);
}

int main() {
	midi_in_device_t* midi = NULL;
	
	SDL_Init(SDL_INIT_AUDIO);

	SDL_AudioSpec want;
	SDL_AudioSpec have;
	SDL_AudioDeviceID device;

	SDL_zero(want);
	want.freq = SAMPLE_RATE;
	want.format = AUDIO_F32;
	want.channels = 2;
	want.samples = 4096;
	want.callback = sdl_audio_callback;

	if (!smol_file_exists("config.txt")) {
		printf("Available audio devices:\n");
		
		//smol_audio_device_t* devices = smol_enumerate_audio_devices(0, &playback_device_count);
		for (int i = 0; i < SDL_GetNumAudioDevices(0); i++) {
			printf("%d) %s\n", i, SDL_GetAudioDeviceName(i, 0));
		}
		printf("Select a device: ");

		int audio_device = 0;
		scanf("%d", &audio_device);

		//smol_audio_playback_init(SAMPLE_RATE, 2, audio_device);
		//smol_audio_set_playback_callback(audio_callback, NULL);
		
		// open audio device
		device = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(audio_device, 0), 0, &want, &have, 0);
		if (device == 0) {
			fprintf(stderr, "Failed to open audio device\n");
			return 1;
		}

		// open midi device
		int midi_devices = midi_get_device_count();
		printf("Available MIDI devices:\n");
		for (int i = 0; i < midi_devices; i++) {
			char name[32];
			midi_get_device_name(i, name);
			printf("%d) %s\n", i, name);
		}
		printf("Select a device: ");

		int midi_device = -1;
		if (scanf("%d", &midi_device) > 0 && midi_device >= 0) {
			midi = midi_open_device(midi_device, midi_callback);
			if (!midi) {
				fprintf(stderr, "Failed to open MIDI device\n");
				return 1;
			}
		}

		FILE* fp = fopen("config.txt", "w");
		fprintf(fp, "%d %d", audio_device, midi_device);
		fclose(fp);
	} else {
		// audio device index (first byte)
		int audio_device = 0;
		// midi device index (second byte)
		int midi_device = 0;
		
		FILE* fp = fopen("config.txt", "r");
		fscanf(fp, "%d %d", &audio_device, &midi_device);
		fclose(fp);

		device = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(audio_device, 0), 0, &want, &have, 0);
		if (device == 0) {
			fprintf(stderr, "Failed to open audio device\n");
			return 1;
		}

		if (midi_device >= 0) {
			midi = midi_open_device(midi_device, midi_callback);
			if (!midi) {
				fprintf(stderr, "Failed to open MIDI device\n");
				return 1;
			}
		}
	}

	// print audio device info (have)
	printf("Audio device info:\n");
	printf("freq: %d\n", have.freq);
	printf("format: %d\n", have.format);
	printf("channels: %d\n", have.channels);
	printf("samples: %d\n", have.samples);

	SDL_PauseAudioDevice(device, 0);

	granular_synth_init(&synth, "aaa.wav");
	synth.sample.window_start = 1.0;
	synth.sample.window_end = synth.sample.window_start + 2.0;
	synth.grain_settings.grains_per_second = 4;
	synth.grain_settings.grain_smoothness = 1.0f;
	synth.random_settings.position_offset_random = 0.1f;
	synth.random_settings.size_random = 0.1f;

	//grain_init(&grain_test);
	//grain_test.pitch = 1.0f;
	//grain_test.velocity = 1.0f;
	//grain_test.position = 0.0f;
	//grain_test.size = 0.5f; // seconds
	//grain_test.state = GRAIN_PLAYING;
	//grain_test.play_mode = GRAIN_PINGPONG;

	//voice_init(&voice_test);
	//voice_test.note_settings.pitch = 1.0f;
	//voice_test.note_settings.velocity = 1.0f;
	//voice_test.grain_settings.position = 0.0f;
	//voice_test.grain_settings.grains_per_second = 8;
	//voice_test.grain_settings.size = 0.5f;
	//voice_test.grain_settings.smoothness = 1.0f;
	//voice_gate(&voice_test, 1);

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

		rect_t root = { 0, 0, frame->width, frame->height };
		rectcut_expand(&root, -5);

		rect_t toolBar = rectcut_top(&root, 30);

		root.y += 5;
		root.height -= 10;

		rect_t waveView = rectcut_top(&root, 256);
		rect_t wvFull = waveView;
		rect_t wvLeft = rectcut_top(&waveView, waveView.height / 2);
		rect_t wvRight = waveView;

		smol_canvas_clear(&canvas, SMOLC_DARKEST_GREY);

		draw_waveform(&canvas, wvLeft, &synth.sample.buffer, 0);
		draw_waveform(&canvas, wvRight, &synth.sample.buffer, 1);

		smol_u32 samplerPerPixel = synth.sample.buffer.num_frames / waveView.width;

		for (int i = 0; i < GS_SYNTH_MAX_VOICES; i++) {
			voice_t* voice = &synth.voices[i];
			if (voice_is_free(voice)) continue;

			for (int j = 0; j < GS_VOICE_MAX_GRAINS; j++) {
				grain_t* grain = &voice->grains[j];
				if (grain_is_free(grain)) continue;

				draw_grain(&canvas, voice, grain, &synth.sample.buffer, wvFull);
			}
		}

		draw_guide(&canvas, "LS", &synth.sample.buffer, synth.sample.window_start, wvFull);
		draw_guide(&canvas, "LE", &synth.sample.buffer, synth.sample.window_end, wvFull);

		gui_begin(&gui);

		static double startTime = 0.1;
		static double endTime = 0.4;

		double maxTime = (double)(synth.sample.buffer.num_frames - 1) / synth.sample.buffer.sample_rate;

		rect_t sampleEndRect = rectcut_right(&toolBar, 150);
		if (gui_spinnerd(&gui, "sampleEnd", sampleEndRect, &endTime, 0.0, maxTime, 0.05, "end: %.2fs")) {
			synth.sample.window_end = endTime;
			if (synth.sample.window_end <= synth.sample.window_start) {
				synth.sample.window_end = synth.sample.window_start + 0.001;
			}
		}

		rect_t sampleStartRect = rectcut_right(&toolBar, 150);
		if (gui_spinnerd(&gui, "sampleStart", sampleStartRect, &startTime, 0.0, endTime, 0.05, "start: %.2fs")) {
			synth.sample.window_start = startTime;
			if (synth.sample.window_start >= synth.sample.window_end) {
				synth.sample.window_start = synth.sample.window_end - 0.001;
				synth.sample.window_start = synth.sample.window_end - 0.001;
			}
		}

		rect_t tuningRect = rectcut_right(&toolBar, 150);
		if (gui_spinnerf(&gui, "tuning", tuningRect, &synth.tuning, -2.0, 2.0, 0.01, "tuning: %.2f")) {
			
		}

		gui_end(&gui);

		//granular_synth_for_each_voice(&synth, draw_grain_info, &canvas);
		//draw_grain_info(0, &voice_test, &canvas);

		//smol_canvas_push_color(&canvas);
		//smol_canvas_set_color(&canvas, SMOLC_WHITE);
		//char state = 'I';
		//switch (grain_test.state) {
		//	case GRAIN_IDLE: state = 'I'; break;
		//	case GRAIN_FINISHED: state = 'F'; break;
		//	case GRAIN_PLAYING: state = '>'; break;
		//}
		//smol_canvas_draw_text_formated(&canvas, 10, 10, 2, "GT: %c%.1f", state, grain_test.time);
		//smol_canvas_pop_color(&canvas);

		smol_canvas_present(&canvas, frame);
	}
	
	if (midi) {
		midi_close_device(midi);
	}

	SDL_CloseAudioDevice(device);
	SDL_Quit();

	//smol_audio_shutdown();
	smol_frame_destroy(frame);

	return 0;
}
