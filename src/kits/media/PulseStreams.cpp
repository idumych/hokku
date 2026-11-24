/*
 * Copyright 2026, the Hokku authors
 * Distributed under the terms of the MIT License.
 */

// See PulseStreams.h. One connection to the sound server per process, on
// a threaded main loop; every call locks the loop, starts an operation and
// waits for its callback to signal.

#include "PulseStreams.h"

#include <MediaDefs.h>
#include <OS.h>

#include <errno.h>
#include <stdlib.h>
#include <mutex>
#include <new>
#include <string.h>

#ifdef HOKKU_HAVE_LIBPULSE
#include <pulse/pulseaudio.h>
#endif


namespace BPrivate { namespace media {

#ifdef HOKKU_HAVE_LIBPULSE

namespace {

pa_threaded_mainloop* sLoop = NULL;
pa_context* sContext = NULL;
std::mutex sConnectLock;


void
context_state(pa_context* context, void*)
{
	(void)context;
	pa_threaded_mainloop_signal(sLoop, 0);
}


// The connection, made the first time it's needed (and again if the server
// went away). Call without the loop locked.
bool
connect_server()
{
	std::lock_guard<std::mutex> guard(sConnectLock);
	if (sLoop == NULL) {
		sLoop = pa_threaded_mainloop_new();
		if (sLoop == NULL || pa_threaded_mainloop_start(sLoop) < 0)
			return false;
	}
	pa_threaded_mainloop_lock(sLoop);
	if (sContext != NULL && pa_context_get_state(sContext) == PA_CONTEXT_READY) {
		pa_threaded_mainloop_unlock(sLoop);
		return true;
	}
	if (sContext != NULL) {
		pa_context_disconnect(sContext);
		pa_context_unref(sContext);
	}
	// named after the program, as its own playback is
	sContext = pa_context_new(pa_threaded_mainloop_get_api(sLoop),
		program_invocation_short_name);
	bool ready = false;
	if (sContext != NULL) {
		pa_context_set_state_callback(sContext, context_state, NULL);
		if (pa_context_connect(sContext, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) >= 0) {
			for (;;) {
				pa_context_state_t state = pa_context_get_state(sContext);
				if (state == PA_CONTEXT_READY) {
					ready = true;
					break;
				}
				if (!PA_CONTEXT_IS_GOOD(state))
					break;
				pa_threaded_mainloop_wait(sLoop);
			}
		}
	}
	pa_threaded_mainloop_unlock(sLoop);
	return ready;
}


// Waits, with the loop locked, until the operation is done
void
wait_for(pa_operation* operation)
{
	if (operation == NULL)
		return;
	while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(sLoop);
	pa_operation_unref(operation);
}


void
add_stream(pa_context*, const pa_sink_input_info* info, int eol, void* cookie)
{
	if (eol != 0 || info == NULL) {
		pa_threaded_mainloop_signal(sLoop, 0);
		return;
	}
	PulseStream stream;
	stream.index = info->index;
	const char* program = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	stream.program = program != NULL ? program
		: (info->client != PA_INVALID_INDEX ? "Unknown program" : "System");
	stream.name = info->name != NULL ? info->name : "";
	const char* pid = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID);
	stream.pid = pid != NULL ? atoi(pid) : -1;
	stream.channels = info->sample_spec.channels;
	stream.rate = info->sample_spec.rate;
	static_cast<std::vector<PulseStream>*>(cookie)->push_back(stream);
}


void
note_sink(pa_context*, const pa_sink_input_info* info, int eol, void* cookie)
{
	if (eol == 0 && info != NULL)
		*static_cast<uint32*>(cookie) = info->sink;
	if (eol != 0)
		pa_threaded_mainloop_signal(sLoop, 0);
}


void
note_monitor(pa_context*, const pa_sink_info* info, int eol, void* cookie)
{
	if (eol == 0 && info != NULL && info->monitor_source_name != NULL)
		*static_cast<BString*>(cookie) = info->monitor_source_name;
	if (eol != 0)
		pa_threaded_mainloop_signal(sLoop, 0);
}


void
signal_done(pa_stream*, int, void*)
{
	pa_threaded_mainloop_signal(sLoop, 0);
}

}	// namespace


bool
pulse_list_streams(std::vector<PulseStream>& streams)
{
	streams.clear();
	if (!connect_server())
		return false;
	pa_threaded_mainloop_lock(sLoop);
	wait_for(pa_context_get_sink_input_info_list(sContext, add_stream, &streams));
	pa_threaded_mainloop_unlock(sLoop);
	return true;
}


struct PulseCapture::Private {
	pa_stream*	stream = NULL;
	DataFunc	func = NULL;
	void*		cookie = NULL;
	std::vector<uint8> silence;

	static void StreamState(pa_stream*, void*)
	{
		pa_threaded_mainloop_signal(sLoop, 0);
	}

	static void Read(pa_stream* stream, size_t, void* cookie)
	{
		Private* self = static_cast<Private*>(cookie);
		while (pa_stream_readable_size(stream) > 0) {
			const void* data = NULL;
			size_t bytes = 0;
			if (pa_stream_peek(stream, &data, &bytes) < 0 || bytes == 0)
				break;
			if (data == NULL) {
				// a hole: silence in its place
				if (self->silence.size() < bytes)
					self->silence.assign(bytes, 0);
				data = self->silence.data();
			}
			self->func(self->cookie, data, bytes);
			pa_stream_drop(stream);
		}
	}
};


PulseCapture::PulseCapture()
	:
	fPrivate(new(std::nothrow) Private)
{
}


PulseCapture::~PulseCapture()
{
	Close();
	delete fPrivate;
}


status_t
PulseCapture::Open(uint32 index, uint32 sampleSize, bool isFloat, uint32 channels,
	uint32 rate, DataFunc func, void* cookie)
{
	if (fPrivate == NULL)
		return B_NO_MEMORY;
	if (fPrivate->stream != NULL)
		return B_BUSY;
	pa_sample_spec spec;
	switch (sampleSize) {
		case 1:
			spec.format = PA_SAMPLE_U8;
			break;
		case 2:
			spec.format = PA_SAMPLE_S16NE;
			break;
		case 4:
			spec.format = isFloat ? PA_SAMPLE_FLOAT32NE : PA_SAMPLE_S32NE;
			break;
		default:
			return B_MEDIA_BAD_FORMAT;
	}
	spec.channels = uint8(channels);
	spec.rate = rate;
	if (!pa_sample_spec_valid(&spec))
		return B_MEDIA_BAD_FORMAT;
	if (!connect_server())
		return B_MEDIA_SYSTEM_FAILURE;

	pa_threaded_mainloop_lock(sLoop);
	// a stream is recorded through the monitor of the sink it plays to
	uint32 sink = PA_INVALID_INDEX;
	wait_for(pa_context_get_sink_input_info(sContext, index, note_sink, &sink));
	BString monitor;
	if (sink != PA_INVALID_INDEX)
		wait_for(pa_context_get_sink_info_by_index(sContext, sink, note_monitor, &monitor));
	if (monitor.Length() == 0) {
		pa_threaded_mainloop_unlock(sLoop);
		return B_NAME_NOT_FOUND;		// it stopped playing
	}

	pa_stream* stream = pa_stream_new(sContext, "Recording", &spec, NULL);
	if (stream == NULL) {
		pa_threaded_mainloop_unlock(sLoop);
		return B_NO_MEMORY;
	}
	fPrivate->func = func;
	fPrivate->cookie = cookie;
	pa_stream_set_state_callback(stream, Private::StreamState, fPrivate);
	pa_stream_set_read_callback(stream, Private::Read, fPrivate);
	pa_stream_set_monitor_stream(stream, index);
	pa_buffer_attr attr;
	memset(&attr, 0xff, sizeof(attr));
	attr.fragsize = uint32(pa_usec_to_bytes(20000, &spec));	// 20 ms
	status_t status = B_MEDIA_SYSTEM_FAILURE;
	if (pa_stream_connect_record(stream, monitor.String(), &attr,
			pa_stream_flags_t(PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY
				| PA_STREAM_DONT_MOVE)) >= 0) {
		for (;;) {
			pa_stream_state_t state = pa_stream_get_state(stream);
			if (state == PA_STREAM_READY) {
				status = B_OK;
				break;
			}
			if (!PA_STREAM_IS_GOOD(state))
				break;
			pa_threaded_mainloop_wait(sLoop);
		}
	}
	if (status == B_OK)
		fPrivate->stream = stream;
	else {
		pa_stream_set_state_callback(stream, NULL, NULL);
		pa_stream_set_read_callback(stream, NULL, NULL);
		pa_stream_disconnect(stream);
		pa_stream_unref(stream);
	}
	pa_threaded_mainloop_unlock(sLoop);
	return status;
}


status_t
PulseCapture::Start()
{
	if (fPrivate == NULL || fPrivate->stream == NULL)
		return B_NO_INIT;
	pa_threaded_mainloop_lock(sLoop);
	wait_for(pa_stream_cork(fPrivate->stream, 0, signal_done, NULL));
	pa_threaded_mainloop_unlock(sLoop);
	return B_OK;
}


void
PulseCapture::Stop()
{
	if (fPrivate == NULL || fPrivate->stream == NULL)
		return;
	pa_threaded_mainloop_lock(sLoop);
	wait_for(pa_stream_cork(fPrivate->stream, 1, signal_done, NULL));
	pa_threaded_mainloop_unlock(sLoop);
}


void
PulseCapture::Close()
{
	if (fPrivate == NULL || fPrivate->stream == NULL)
		return;
	pa_threaded_mainloop_lock(sLoop);
	pa_stream_set_state_callback(fPrivate->stream, NULL, NULL);
	pa_stream_set_read_callback(fPrivate->stream, NULL, NULL);
	pa_stream_disconnect(fPrivate->stream);
	pa_stream_unref(fPrivate->stream);
	fPrivate->stream = NULL;
	pa_threaded_mainloop_unlock(sLoop);
}

#else	// no PulseAudio client library: no other programs to record

bool
pulse_list_streams(std::vector<PulseStream>& streams)
{
	streams.clear();
	return false;
}


struct PulseCapture::Private {};

PulseCapture::PulseCapture() : fPrivate(NULL) {}
PulseCapture::~PulseCapture() {}

status_t
PulseCapture::Open(uint32, uint32, bool, uint32, uint32, DataFunc, void*)
{
	return B_NOT_SUPPORTED;
}

status_t PulseCapture::Start() { return B_NO_INIT; }
void PulseCapture::Stop() {}
void PulseCapture::Close() {}

#endif

} }	// BPrivate::media
