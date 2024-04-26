#pragma once

#include <windows.h>
#include <mmeapi.h>
#pragma comment(lib, "winmm.lib")

enum midi_message_status {
	MIDI_NOTE_OFF = 0x8,
	MIDI_NOTE_ON = 0x9,
	MIDI_CONTROL_CHANGE = 0xB,
	MIDI_PITCH_BEND = 0xE,
	MIDI_SYSTEM = 0xF
};

typedef struct midi_message_t {
	int status;
	int channel;
	
	union {
		struct {
			BYTE data1, data2;
		};

		struct {
			// note on and note off
			BYTE pitch, velocity;
		} note;

		struct {
			// control change
			BYTE controller, value;
		} control_change;

		WORD pitch_bend_value;
	};
} midi_message_t;

typedef void (*midi_callback_t)(midi_message_t);

typedef struct midi_in_device_t {
	HMIDIIN handle;
	midi_callback_t callback;
} midi_in_device_t;

int midi_get_device_count();
void midi_get_device_name(int device, char* out);
midi_in_device_t* midi_open_device(int device, midi_callback_t callback);
void midi_close_device(midi_in_device_t* device);
