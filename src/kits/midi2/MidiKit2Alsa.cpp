/*
 * Copyright 2026, the Hokku authors
 * Distributed under the terms of the MIT License.
 */

// The Midi Kit 2 producer side on the ALSA sequencer, so a Haiku program's
// MIDI reaches Linux programs (Hydrogen, Ardour, synths) and hardware ports:
//   - the process is one sequencer client, named after the program;
//   - each BMidiLocalProducer is one of its ports, named after the endpoint,
//     which others can subscribe to once it's registered (Register());
//   - Spray*() with a time sends the event through a sequencer queue at that
//     system_time(), as Haiku's consumers would get it; time 0 or a time
//     already past sends it at once.
// The connections are ALSA subscriptions, made by the other program or by
// aconnect/qpwgraph, not by BMidiProducer::Connect().
//
// shortcut: producers only. There's no BMidiRoster, no consumers (so no
// MidiConsumer.h or MidiRoster.h is installed), and Connected() or
// Disconnected() are never called. Add consumers on ALSA input ports when
// an app needs to receive MIDI.

#include <MidiProducer.h>

#include <alsa/asoundlib.h>

#include <errno.h>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <vector>


namespace {

struct Sequencer {
	std::mutex	lock;
	bool		tried = false;
	snd_seq_t*	seq = NULL;
	int			client = -1;
	int			queue = -1;
	bigtime_t	base = 0;		// system_time() at the queue's time 0
};

Sequencer sSequencer;


// The client and its queue, opened the first time a producer is made.
// Call with sSequencer.lock held.
bool
open_sequencer()
{
	Sequencer& s = sSequencer;
	if (s.tried)
		return s.seq != NULL;
	s.tried = true;
	if (snd_seq_open(&s.seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
		s.seq = NULL;
		return false;
	}
	snd_seq_set_client_name(s.seq, program_invocation_short_name);
	s.client = snd_seq_client_id(s.seq);
	s.queue = snd_seq_alloc_named_queue(s.seq, "Midi Kit");
	if (s.queue >= 0) {
		snd_seq_start_queue(s.seq, s.queue, NULL);
		snd_seq_drain_output(s.seq);
		// where the queue's clock stands now, against system_time()
		snd_seq_queue_status_t* status;
		snd_seq_queue_status_alloca(&status);
		bigtime_t now = system_time();
		if (snd_seq_get_queue_status(s.seq, s.queue, status) == 0) {
			const snd_seq_real_time_t* t = snd_seq_queue_status_get_real_time(status);
			s.base = now - (bigtime_t(t->tv_sec) * 1000000 + t->tv_nsec / 1000);
		} else
			s.base = now;
	}
	return true;
}


int
port_of(int32 id)
{
	return id & 0xff;
}

}	// namespace


// #pragma mark - BMidiEndpoint


BMidiEndpoint::BMidiEndpoint(const char* name)
	:
	fId(0),
	fName(name),
	fRefCount(1),
	fProperties(new BMessage),
	fIsLocal(false),
	fIsConsumer(false),
	fIsRegistered(false),
	fIsAlive(true)
{
}


BMidiEndpoint::~BMidiEndpoint()
{
	delete fProperties;
}


const char*
BMidiEndpoint::Name() const
{
	return fName.String();
}


void
BMidiEndpoint::SetName(const char* name)
{
	fName = name;
	std::lock_guard<std::mutex> guard(sSequencer.lock);
	if (fId == 0 || sSequencer.seq == NULL)
		return;
	snd_seq_port_info_t* info;
	snd_seq_port_info_alloca(&info);
	if (snd_seq_get_port_info(sSequencer.seq, port_of(fId), info) == 0) {
		snd_seq_port_info_set_name(info, fName.String());
		snd_seq_set_port_info(sSequencer.seq, port_of(fId), info);
	}
}


int32
BMidiEndpoint::ID() const
{
	return fId;
}


bool BMidiEndpoint::IsProducer() const { return !fIsConsumer; }
bool BMidiEndpoint::IsConsumer() const { return fIsConsumer; }
bool BMidiEndpoint::IsRemote() const { return !fIsLocal; }
bool BMidiEndpoint::IsLocal() const { return fIsLocal; }
bool BMidiEndpoint::IsPersistent() const { return false; }


bool
BMidiEndpoint::IsValid() const
{
	return fIsAlive && fId != 0;
}


status_t
BMidiEndpoint::Release()
{
	if (--fRefCount > 0)
		return B_OK;
	if (fIsRegistered)
		Unregister();
	delete this;
	return B_OK;
}


status_t
BMidiEndpoint::Acquire()
{
	fRefCount++;
	return B_OK;
}


status_t
BMidiEndpoint::SetProperties(const BMessage* properties)
{
	if (properties == NULL)
		return B_BAD_VALUE;
	*fProperties = *properties;
	return B_OK;
}


status_t
BMidiEndpoint::GetProperties(BMessage* properties) const
{
	if (properties == NULL)
		return B_BAD_VALUE;
	*properties = *fProperties;
	return B_OK;
}


status_t
BMidiEndpoint::Register()
{
	return SendRegisterRequest(true);
}


status_t
BMidiEndpoint::Unregister()
{
	return SendRegisterRequest(false);
}


// Registered, others can subscribe to the port; unregistered, they can't
// (the port stays, so it keeps its ID and spraying goes on working)
status_t
BMidiEndpoint::SendRegisterRequest(bool registered)
{
	if (!fIsLocal || !IsValid())
		return B_ERROR;
	std::lock_guard<std::mutex> guard(sSequencer.lock);
	snd_seq_port_info_t* info;
	snd_seq_port_info_alloca(&info);
	if (snd_seq_get_port_info(sSequencer.seq, port_of(fId), info) != 0)
		return B_ERROR;
	unsigned int caps = SND_SEQ_PORT_CAP_READ;
	if (registered)
		caps |= SND_SEQ_PORT_CAP_SUBS_READ;
	snd_seq_port_info_set_capability(info, caps);
	if (snd_seq_set_port_info(sSequencer.seq, port_of(fId), info) != 0)
		return B_ERROR;
	fIsRegistered = registered;
	return B_OK;
}


status_t
BMidiEndpoint::SendChangeRequest(BMessage* message)
{
	(void)message;
	return B_OK;
}


bool
BMidiEndpoint::IsRegistered() const
{
	return fIsRegistered;
}


bool
BMidiEndpoint::LockLooper() const
{
	return true;
}


void
BMidiEndpoint::UnlockLooper() const
{
}


void BMidiEndpoint::_Reserved1() {}
void BMidiEndpoint::_Reserved2() {}
void BMidiEndpoint::_Reserved3() {}
void BMidiEndpoint::_Reserved4() {}
void BMidiEndpoint::_Reserved5() {}
void BMidiEndpoint::_Reserved6() {}
void BMidiEndpoint::_Reserved7() {}
void BMidiEndpoint::_Reserved8() {}


// #pragma mark - BMidiProducer


BMidiProducer::BMidiProducer(const char* name)
	:
	BMidiEndpoint(name),
	fConnections(new BList),
	fLocker("BMidiProducer")
{
}


BMidiProducer::~BMidiProducer()
{
	delete fConnections;
}


status_t
BMidiProducer::Connect(BMidiConsumer* consumer)
{
	(void)consumer;
	return B_ERROR;		// no consumers here: other programs subscribe
}


status_t
BMidiProducer::Disconnect(BMidiConsumer* consumer)
{
	(void)consumer;
	return B_ERROR;
}


bool
BMidiProducer::IsConnected(BMidiConsumer* consumer) const
{
	(void)consumer;
	return false;
}


BList*
BMidiProducer::Connections() const
{
	return new BList;
}


status_t
BMidiProducer::SendConnectRequest(BMidiConsumer* consumer, bool connect)
{
	(void)consumer;
	(void)connect;
	return B_ERROR;
}


void BMidiProducer::ConnectionMade(BMidiConsumer*) {}
bool BMidiProducer::ConnectionBroken(BMidiConsumer*) { return false; }
int32 BMidiProducer::CountConsumers() const { return 0; }
BMidiConsumer* BMidiProducer::ConsumerAt(int32) const { return NULL; }
bool BMidiProducer::LockProducer() const { return fLocker.Lock(); }
void BMidiProducer::UnlockProducer() const { fLocker.Unlock(); }

void BMidiProducer::_Reserved1() {}
void BMidiProducer::_Reserved2() {}
void BMidiProducer::_Reserved3() {}
void BMidiProducer::_Reserved4() {}
void BMidiProducer::_Reserved5() {}
void BMidiProducer::_Reserved6() {}
void BMidiProducer::_Reserved7() {}
void BMidiProducer::_Reserved8() {}


// #pragma mark - BMidiLocalProducer


BMidiLocalProducer::BMidiLocalProducer(const char* name)
	:
	BMidiProducer(name)
{
	fIsLocal = true;
	std::lock_guard<std::mutex> guard(sSequencer.lock);
	if (!open_sequencer())
		return;
	// readable, but not subscribable until Register()
	int port = snd_seq_create_simple_port(sSequencer.seq,
		name != NULL && name[0] != '\0' ? name : program_invocation_short_name,
		SND_SEQ_PORT_CAP_READ, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	if (port >= 0)
		fId = sSequencer.client << 8 | port;
}


BMidiLocalProducer::~BMidiLocalProducer()
{
	std::lock_guard<std::mutex> guard(sSequencer.lock);
	if (fId != 0 && sSequencer.seq != NULL)
		snd_seq_delete_simple_port(sSequencer.seq, port_of(fId));
}


void BMidiLocalProducer::Connected(BMidiConsumer*) {}
void BMidiLocalProducer::Disconnected(BMidiConsumer*) {}


void
BMidiLocalProducer::SprayData(void* data, size_t length, bool atomic, bigtime_t time) const
{
	SprayEvent(data, length, atomic, time);
}


static bool
channel_ok(uchar channel)
{
	if (channel < 16)
		return true;
	fprintf(stderr, "BMidiLocalProducer: invalid MIDI channel %d\n", channel);
	return false;
}


void
BMidiLocalProducer::SprayNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time) const
{
	uchar data[3] = { uchar(B_NOTE_OFF + channel), note, velocity };
	if (channel_ok(channel))
		SprayEvent(data, 3, true, time);
}


void
BMidiLocalProducer::SprayNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time) const
{
	uchar data[3] = { uchar(B_NOTE_ON + channel), note, velocity };
	if (channel_ok(channel))
		SprayEvent(data, 3, true, time);
}


void
BMidiLocalProducer::SprayKeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time) const
{
	uchar data[3] = { uchar(B_KEY_PRESSURE + channel), note, pressure };
	if (channel_ok(channel))
		SprayEvent(data, 3, true, time);
}


void
BMidiLocalProducer::SprayControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time) const
{
	uchar data[3] = { uchar(B_CONTROL_CHANGE + channel), controlNumber, controlValue };
	if (channel_ok(channel))
		SprayEvent(data, 3, true, time);
}


void
BMidiLocalProducer::SprayProgramChange(uchar channel, uchar programNumber,
	bigtime_t time) const
{
	uchar data[2] = { uchar(B_PROGRAM_CHANGE + channel), programNumber };
	if (channel_ok(channel))
		SprayEvent(data, 2, true, time);
}


void
BMidiLocalProducer::SprayChannelPressure(uchar channel, uchar pressure,
	bigtime_t time) const
{
	uchar data[2] = { uchar(B_CHANNEL_PRESSURE + channel), pressure };
	if (channel_ok(channel))
		SprayEvent(data, 2, true, time);
}


void
BMidiLocalProducer::SprayPitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time) const
{
	uchar data[3] = { uchar(B_PITCH_BEND + channel), lsb, msb };
	if (channel_ok(channel))
		SprayEvent(data, 3, true, time);
}


void
BMidiLocalProducer::SpraySystemExclusive(void* data, size_t length,
	bigtime_t time) const
{
	SprayEvent(data, length, true, time, true);
}


// The lengths are Haiku's
void
BMidiLocalProducer::SpraySystemCommon(uchar status, uchar data1, uchar data2,
	bigtime_t time) const
{
	uchar data[3] = { status, data1, data2 };
	size_t length;
	switch (status) {
		case B_TUNE_REQUEST:
		case B_SYS_EX_END:
			length = 1;
			break;
		case B_CABLE_MESSAGE:
		case B_MIDI_TIME_CODE:
		case B_SONG_SELECT:
			length = 2;
			break;
		case B_SONG_POSITION:
			length = 3;
			break;
		default:
			fprintf(stderr, "BMidiLocalProducer: invalid system common status %#x\n",
				status);
			return;
	}
	SprayEvent(data, length, true, time);
}


void
BMidiLocalProducer::SpraySystemRealTime(uchar status, bigtime_t time) const
{
	if (status < B_TIMING_CLOCK) {
		fprintf(stderr, "BMidiLocalProducer: invalid real time status %#x\n", status);
		return;
	}
	SprayEvent(&status, 1, true, time);
}


// Haiku sends its consumers a tempo meta event (FF 51 03 tttttt), which has
// no form on a MIDI wire: 0xFF there is a System Reset. Nothing is sent.
void
BMidiLocalProducer::SprayTempoChange(int32 beatsPerMinute, bigtime_t time) const
{
	(void)beatsPerMinute;
	(void)time;
}


// The bytes as sequencer events, from this producer's port to everyone
// subscribed to it, at `time` (through the queue) or now
void
BMidiLocalProducer::SprayEvent(const void* data, size_t length, bool atomic,
	bigtime_t time, bool sysex) const
{
	(void)atomic;
	if (fId == 0 || data == NULL || length == 0)
		return;
	std::lock_guard<std::mutex> guard(sSequencer.lock);
	Sequencer& s = sSequencer;
	if (s.seq == NULL)
		return;

	auto send = [&](snd_seq_event_t& event) {
		snd_seq_ev_set_source(&event, port_of(fId));
		snd_seq_ev_set_subs(&event);
		if (time > system_time() && s.queue >= 0) {
			const bigtime_t at = time - s.base;
			snd_seq_real_time_t when;
			when.tv_sec = (unsigned int)(at / 1000000);
			when.tv_nsec = (unsigned int)(at % 1000000) * 1000;
			snd_seq_ev_schedule_real(&event, s.queue, 0, &when);
		} else
			snd_seq_ev_set_direct(&event);
		snd_seq_event_output(s.seq, &event);
	};

	if (sysex) {
		// SpraySystemExclusive's data has no F0 and F7: the event needs them
		std::vector<uint8> bytes(length + 2);
		bytes[0] = B_SYS_EX_START;
		memcpy(bytes.data() + 1, data, length);
		bytes[length + 1] = B_SYS_EX_END;
		snd_seq_event_t event;
		snd_seq_ev_clear(&event);
		snd_seq_ev_set_sysex(&event, bytes.size(), bytes.data());
		send(event);
	} else {
		snd_midi_event_t* parser;
		if (snd_midi_event_new(length > 256 ? length : 256, &parser) < 0)
			return;
		const uint8* bytes = static_cast<const uint8*>(data);
		size_t done = 0;
		while (done < length) {
			snd_seq_event_t event;
			snd_seq_ev_clear(&event);
			long used = snd_midi_event_encode(parser, bytes + done, long(length - done),
				&event);
			if (used <= 0)
				break;
			done += size_t(used);
			if (event.type != SND_SEQ_EVENT_NONE)
				send(event);
		}
		snd_midi_event_free(parser);
	}
	snd_seq_drain_output(s.seq);
}


void BMidiLocalProducer::_Reserved1() {}
void BMidiLocalProducer::_Reserved2() {}
void BMidiLocalProducer::_Reserved3() {}
void BMidiLocalProducer::_Reserved4() {}
void BMidiLocalProducer::_Reserved5() {}
void BMidiLocalProducer::_Reserved6() {}
void BMidiLocalProducer::_Reserved7() {}
void BMidiLocalProducer::_Reserved8() {}
