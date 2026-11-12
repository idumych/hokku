/*
 * Copyright 2026, the Hokku authors
 * Distributed under the terms of the MIT License.
 */

// BMediaRecorder records from the default audio input through miniaudio,
// as SoundPlayer.cpp plays through it. There are no media nodes here: every
// Connect() opens the default capture device in the format asked for (raw
// audio; miniaudio converts), and the record hook gets each period's frames
// on miniaudio's audio thread, as Haiku's hook is called on its node's.
//
// COSMOE_AUDIO_BACKEND=null records silence, paced in real time, and
// COSMOE_AUDIO_CAPTURE_FILE=<sound file> records that file instead of the
// input (then silence), for tests and headless runs.

#include <third_party/miniaudio/miniaudio.h>

#include <MediaRecorder.h>

#include <OS.h>

#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>


namespace BPrivate { namespace media {

class BMediaRecorderNode {
public:
	BMediaRecorderNode()
		:
		deviceOpen(false),
		contextOpen(false),
		decoderOpen(false),
		feeder(-1),
		feeding(false),
		recorder(NULL)
	{
		accepted.type = B_MEDIA_RAW_AUDIO;
		accepted.u.raw_audio = media_multi_audio_format::wildcard;
	}

	media_format	accepted;
	media_format	format;
	ma_device		device;
	ma_context		context;
	ma_decoder		decoder;
	bool			deviceOpen;
	bool			contextOpen;
	bool			decoderOpen;
	thread_id		feeder;		// plays the capture file in real time
	std::atomic<bool> feeding;
	std::atomic<bool> running;
	BMediaRecorder*	recorder;
	uint32			frameSize;
	uint32			period;		// frames per hook call

	// BMediaRecorder's BufferReceived (protected; this class is its friend)
	void Deliver(void* data, ma_uint32 frames)
	{
		if (!running || recorder == NULL || frames == 0)
			return;
		media_header header;
		memset(&header, 0, sizeof(header));
		header.type = B_MEDIA_RAW_AUDIO;
		header.size_used = frames * frameSize;
		header.start_time = system_time();
		recorder->BufferReceived(data, header.size_used, header);
	}
};

}	// namespace media
}	// namespace BPrivate

using BPrivate::media::BMediaRecorderNode;


static void
capture_callback(ma_device* device, void* output, const void* input,
	ma_uint32 frames)
{
	(void)output;
	BMediaRecorderNode* node = (BMediaRecorderNode*)device->pUserData;
	node->Deliver(const_cast<void*>(input), frames);
}


// COSMOE_AUDIO_CAPTURE_FILE: the file's frames in the recorder's format,
// one period at a time, at the rate a device would deliver them
static status_t
feed_file(void* cookie)
{
	BMediaRecorderNode* node = (BMediaRecorderNode*)cookie;
	std::vector<uint8> buffer(node->period * node->frameSize);
	const bigtime_t interval = bigtime_t(node->period) * 1000000
		/ bigtime_t(node->format.u.raw_audio.frame_rate);
	bigtime_t next = system_time();
	while (node->feeding) {
		ma_uint64 got = 0;
		memset(buffer.data(), 0, buffer.size());
		if (node->decoderOpen)
			ma_decoder_read_pcm_frames(&node->decoder, buffer.data(), node->period, &got);
		node->Deliver(buffer.data(), node->period);
		next += interval;
		bigtime_t wait = next - system_time();
		if (wait > 0)
			snooze(wait);
	}
	return B_OK;
}


BMediaRecorder::BMediaRecorder(const char* name, media_type type)
	:
	fInitErr(B_OK),
	fConnected(false),
	fRunning(false),
	fReleaseOutputNode(false),
	fRecordHook(NULL),
	fNotifyHook(NULL),
	fNode(NULL),
	fBufferCookie(NULL)
{
	(void)name;
	if (type != B_MEDIA_UNKNOWN_TYPE && type != B_MEDIA_RAW_AUDIO) {
		// only audio can be recorded here
		fInitErr = B_MEDIA_BAD_FORMAT;
		return;
	}
	fNode = new(std::nothrow) BMediaRecorderNode;
	if (fNode == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	fNode->recorder = this;
	fNode->running = false;
}


BMediaRecorder::~BMediaRecorder()
{
	Disconnect();
	delete fNode;
}


status_t
BMediaRecorder::InitCheck() const
{
	return fInitErr;
}


status_t
BMediaRecorder::SetHooks(ProcessFunc recordFunc, NotifyFunc notifyFunc,
	void* cookie)
{
	fRecordHook = recordFunc;
	fNotifyHook = notifyFunc;
	fBufferCookie = cookie;
	return B_OK;
}


void
BMediaRecorder::SetAcceptedFormat(const media_format& format)
{
	if (fNode != NULL)
		fNode->accepted = format;
}


const media_format&
BMediaRecorder::AcceptedFormat() const
{
	static media_format empty;
	return fNode != NULL ? fNode->accepted : empty;
}


status_t
BMediaRecorder::Start(bool force)
{
	(void)force;
	if (fInitErr != B_OK)
		return fInitErr;
	if (!fConnected)
		return B_MEDIA_NOT_CONNECTED;
	if (fRunning)
		return B_OK;
	if (fNotifyHook != NULL)
		(*fNotifyHook)(fBufferCookie, B_WILL_START, system_time());
	fNode->running = true;
	if (fNode->deviceOpen) {
		if (ma_device_start(&fNode->device) != MA_SUCCESS) {
			fNode->running = false;
			return B_ERROR;
		}
	} else {
		fNode->feeding = true;
		fNode->feeder = spawn_thread(feed_file, "capture file", B_REAL_TIME_PRIORITY, fNode);
		if (fNode->feeder < 0 || resume_thread(fNode->feeder) != B_OK) {
			fNode->feeding = false;
			fNode->running = false;
			return B_ERROR;
		}
	}
	fRunning = true;
	return B_OK;
}


status_t
BMediaRecorder::Stop(bool force)
{
	(void)force;
	if (!fRunning)
		return B_OK;
	if (fNotifyHook != NULL)
		(*fNotifyHook)(fBufferCookie, B_WILL_STOP, system_time(), true);
	fNode->running = false;
	if (fNode->deviceOpen)
		ma_device_stop(&fNode->device);
	if (fNode->feeder >= 0) {
		fNode->feeding = false;
		status_t result;
		wait_for_thread(fNode->feeder, &result);
		fNode->feeder = -1;
	}
	fRunning = false;
	return B_OK;
}


// The format: what the caller asks for, with the accepted format's and the
// defaults' values where it leaves a wildcard (mono, 16-bit, 44.1 kHz)
status_t
BMediaRecorder::Connect(const media_format& requested)
{
	if (fInitErr != B_OK)
		return fInitErr;
	if (fConnected)
		return B_MEDIA_ALREADY_CONNECTED;
	if (requested.type != B_MEDIA_RAW_AUDIO && requested.type != B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;

	media_multi_audio_format raw = requested.u.raw_audio;
	const media_multi_audio_format& accepted = fNode->accepted.u.raw_audio;
	const media_multi_audio_format& wild = media_multi_audio_format::wildcard;
	if (raw.frame_rate == wild.frame_rate)
		raw.frame_rate = accepted.frame_rate != wild.frame_rate ? accepted.frame_rate : 44100;
	if (raw.channel_count == wild.channel_count)
		raw.channel_count = accepted.channel_count != wild.channel_count
			? accepted.channel_count : 1;
	if (raw.format == wild.format)
		raw.format = accepted.format != wild.format ? accepted.format
			: media_raw_audio_format::B_AUDIO_SHORT;
	ma_format sampleFormat;
	uint32 sampleSize;
	switch (raw.format) {
		case media_raw_audio_format::B_AUDIO_SHORT:
			sampleFormat = ma_format_s16;
			sampleSize = 2;
			break;
		case media_raw_audio_format::B_AUDIO_FLOAT:
			sampleFormat = ma_format_f32;
			sampleSize = 4;
			break;
		case media_raw_audio_format::B_AUDIO_INT:
			sampleFormat = ma_format_s32;
			sampleSize = 4;
			break;
		case media_raw_audio_format::B_AUDIO_UCHAR:
			sampleFormat = ma_format_u8;
			sampleSize = 1;
			break;
		default:
			return B_MEDIA_BAD_FORMAT;
	}
	fNode->period = 1024;
	raw.byte_order = B_MEDIA_HOST_ENDIAN;
	raw.buffer_size = fNode->period * sampleSize * raw.channel_count;
	fNode->frameSize = sampleSize * raw.channel_count;
	fNode->format.Clear();
	fNode->format.type = B_MEDIA_RAW_AUDIO;
	fNode->format.u.raw_audio = raw;

	const char* file = getenv("COSMOE_AUDIO_CAPTURE_FILE");
	if (file != NULL && file[0] != '\0') {
		ma_decoder_config config = ma_decoder_config_init(sampleFormat, raw.channel_count,
			(ma_uint32)raw.frame_rate);
		fNode->decoderOpen = ma_decoder_init_file(file, &config, &fNode->decoder) == MA_SUCCESS;
		if (!fNode->decoderOpen)
			return B_ENTRY_NOT_FOUND;
		fConnected = true;
		return B_OK;
	}

	ma_device_config config = ma_device_config_init(ma_device_type_capture);
	config.capture.format = sampleFormat;
	config.capture.channels = raw.channel_count;
	config.sampleRate = (ma_uint32)raw.frame_rate;
	config.periodSizeInFrames = fNode->period;
	config.dataCallback = capture_callback;
	config.pUserData = fNode;

	ma_context* context = NULL;
	const char* backend = getenv("COSMOE_AUDIO_BACKEND");
	if (backend != NULL && strcmp(backend, "null") == 0) {
		ma_backend backends[] = { ma_backend_null };
		if (ma_context_init(backends, 1, NULL, &fNode->context) != MA_SUCCESS)
			return B_ERROR;
		fNode->contextOpen = true;
		context = &fNode->context;
	}
	if (ma_device_init(context, &config, &fNode->device) != MA_SUCCESS) {
		if (fNode->contextOpen) {
			ma_context_uninit(&fNode->context);
			fNode->contextOpen = false;
		}
		return B_MEDIA_SYSTEM_FAILURE;
	}
	fNode->deviceOpen = true;
	fConnected = true;
	return B_OK;
}


status_t
BMediaRecorder::Connect(const dormant_node_info& dormantInfo, const media_format& format)
{
	(void)dormantInfo;
	return Connect(format);
}


status_t
BMediaRecorder::Connect(const media_node& node, const media_output* output,
	const media_format* format)
{
	(void)node;
	(void)output;
	return Connect(format != NULL ? *format : fNode->accepted);
}


status_t
BMediaRecorder::Disconnect()
{
	if (fNode == NULL || !fConnected)
		return B_OK;
	Stop();
	if (fNode->deviceOpen) {
		ma_device_uninit(&fNode->device);
		fNode->deviceOpen = false;
	}
	if (fNode->contextOpen) {
		ma_context_uninit(&fNode->context);
		fNode->contextOpen = false;
	}
	if (fNode->decoderOpen) {
		ma_decoder_uninit(&fNode->decoder);
		fNode->decoderOpen = false;
	}
	fConnected = false;
	return B_OK;
}


bool
BMediaRecorder::IsConnected() const
{
	return fConnected;
}


bool
BMediaRecorder::IsRunning() const
{
	return fRunning;
}


const media_format&
BMediaRecorder::Format() const
{
	static media_format empty;
	return fNode != NULL && fConnected ? fNode->format : empty;
}


const media_source&
BMediaRecorder::MediaSource() const
{
	return media_source::null;
}


const media_input
BMediaRecorder::MediaInput() const
{
	return media_input();
}


void
BMediaRecorder::BufferReceived(void* buffer, size_t size, const media_header& header)
{
	if (fRecordHook != NULL)
		(*fRecordHook)(fBufferCookie, header.start_time, buffer, size, Format());
}


status_t
BMediaRecorder::SetUpConnection(media_source outputSource)
{
	(void)outputSource;
	return B_NOT_SUPPORTED;
}


status_t
BMediaRecorder::_Connect(const media_node& mediaNode, const media_output* output,
	const media_format& format)
{
	return Connect(mediaNode, output, &format);
}


void BMediaRecorder::_ReservedMediaRecorder0() {}
void BMediaRecorder::_ReservedMediaRecorder1() {}
void BMediaRecorder::_ReservedMediaRecorder2() {}
void BMediaRecorder::_ReservedMediaRecorder3() {}
void BMediaRecorder::_ReservedMediaRecorder4() {}
void BMediaRecorder::_ReservedMediaRecorder5() {}
void BMediaRecorder::_ReservedMediaRecorder6() {}
void BMediaRecorder::_ReservedMediaRecorder7() {}
