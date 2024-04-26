#include "midi.h"
#include <stdio.h>

static const char* mm_sys_err_string[] = {
	"no error",
	"unspecified error",
	"device ID out of range",
	"driver failed enable",
	"device already allocated",
	"device handle is invalid",
	"no device driver present",
	"memory allocation error",
	"function isn't supported",
	"error value out of range",
	"invalid flag passed",
	"invalid parameter passed",
	"handle being used",
	"specified alias not found",
	"bad registry database",
	"registry key not found",
	"registry read error",
	"registry write error",
	"registry delete error",
	"registry value not found",
	"driver does not call DriverCallback",
	"more data to be returned",
	"last error in range"
};

static void CALLBACK midiInProc(
	HMIDIIN hMidiIn,
	UINT wMsg,
	DWORD_PTR dwInstance,
	DWORD_PTR dwParam1, DWORD_PTR dwParam2
) {
	if (wMsg == MIM_DATA) {
		// MIM_DATA 
		// dwParam1 = dwMidiMessage
		// dwParam2 = dwTimestamp

		// dwMidiMessage
		// high word:
		//     high byte: unused
		//     low byte: second byte of MIDI data (when needed).
		// low word:
		//     high byte: first byte of MIDI data (when needed).
		//     low byte: status byte.

		midi_in_device_t* device = (midi_in_device_t*)dwInstance;

		BYTE status = (BYTE)(dwParam1 & 0x000000FF);
		BYTE byte1 = (BYTE)((dwParam1 & 0x0000FF00) >> 8);
		BYTE byte2 = (BYTE)((dwParam1 & 0x00FF0000) >> 16);

		midi_message_t message;
		message.channel = status & 0x0F;
		message.status = (status & 0xF0) >> 4;
		message.data1 = byte1;
		message.data2 = byte2;

		if (device->callback) {
			device->callback(message);
		}
	}
}

int midi_get_device_count() {
	return midiInGetNumDevs();
}

void midi_get_device_name(int device, char* out) {
	MIDIINCAPSA caps;
	midiInGetDevCapsA(device, &caps, sizeof(MIDIINCAPSA));
	strcpy(out, caps.szPname);
}

midi_in_device_t* midi_open_device(int device, midi_callback_t callback) {
	HMIDIIN handle;

	midi_in_device_t* result = (midi_in_device_t*)malloc(sizeof(midi_in_device_t));
	result->callback = callback;

	MMRESULT res = midiInOpen(
		&handle,
		device,
		(DWORD_PTR)(void*)midiInProc,
		(DWORD_PTR)(void*)result,
		CALLBACK_FUNCTION | MIDI_IO_STATUS
	);
	if (res != MMSYSERR_NOERROR) {
		fprintf(stderr, "midiInOpen failed: %s\n", mm_sys_err_string[res]);
		free(result);
		return NULL;
	}
	
	res = midiInStart(handle);
	if (res != MMSYSERR_NOERROR) {
		fprintf(stderr, "midiInStart failed: %s\n", mm_sys_err_string[res]);
		midiInClose(handle);
		free(result);
		return NULL;
	}

	return result;
}

void midi_close_device(midi_in_device_t* device) {
	MMRESULT res = midiInStop(device->handle);
	if (res != MMSYSERR_NOERROR) {
		fprintf(stderr, "midiInStop failed (not stopped): %s\n", mm_sys_err_string[res]);
		return;
	}

	res = midiInClose(device->handle);
	if (res != MMSYSERR_NOERROR) {
		fprintf(stderr, "midiInClose failed: %s\n", mm_sys_err_string[res]);
	}
	free(device);
}
