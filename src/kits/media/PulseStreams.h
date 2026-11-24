/*
 * Copyright 2026, the Hokku authors
 * Distributed under the terms of the MIT License.
 */
#ifndef _HOKKU_PULSE_STREAMS_H
#define _HOKKU_PULSE_STREAMS_H

// Other programs' sound, through the PulseAudio client library (PipeWire
// answers it too): the streams programs are playing, and recording one of
// them on its own, as pavucontrol's level meters do. BMediaRoster shows
// each program as a producer node and BMediaRecorder records from it.

#include <String.h>
#include <SupportDefs.h>

#include <vector>


namespace BPrivate { namespace media {

// BMediaRoster's node IDs: the default audio input, then one per program
// playing sound, numbered from kFirstProgramNode in the order they're seen
const int32 kAudioInputNode = 1;
const int32 kFirstProgramNode = 100;

struct PulseStream {
	uint32		index;			// the sink input
	BString		program;		// application.name
	BString		name;			// media.name ("Playback", "Send"...)
	int32		pid;			// application.process.id, or -1
	uint32		channels;
	uint32		rate;
};

// The streams playing now. False when there is no sound server to ask.
bool pulse_list_streams(std::vector<PulseStream>& streams);

// Records one stream. Data arrives on the client library's thread, in the
// sample format, channels and rate asked for (the server converts).
class PulseCapture {
public:
	typedef void (*DataFunc)(void* cookie, const void* data, size_t bytes);

								PulseCapture();
								~PulseCapture();

	// sampleSize 1 (unsigned 8-bit), 2 (16-bit), 4 with isFloat (float) or
	// 4 (32-bit integer)
			status_t			Open(uint32 stream, uint32 sampleSize, bool isFloat,
									uint32 channels, uint32 rate, DataFunc func,
									void* cookie);
			status_t			Start();
			void				Stop();
			void				Close();

private:
	struct Private;
			Private*			fPrivate;
};

} }	// BPrivate::media

#endif
