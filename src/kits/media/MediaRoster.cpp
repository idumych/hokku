/*
 * Copyright 2026, the Hokku authors
 * Distributed under the terms of the MIT License.
 */

// BMediaRoster, for finding something to record. There is no media server
// and no node graph here; the roster shows what can be recorded as Haiku's
// would show producer nodes:
//   - the default audio input (BMediaRecorder records it through miniaudio),
//   - each program playing sound, with one output per stream it plays
//     (PulseStreams.h; recorded on its own, the program goes on playing).
// Every output is free: recording a stream doesn't take it from anyone.
//
// shortcut: only the calls that find and hold nodes are here (Roster,
// GetAudioInput, GetLiveNodes, GetLiveNodeInfo, Get*OutputsFor, GetNodeFor,
// ReleaseNode); a program that builds node chains with Connect, Start or
// the time sources won't link. Add them when an app needs a node graph.

#include <MediaRoster.h>

#include <Autolock.h>
#include <Locker.h>
#include <MediaNode.h>

#include <string.h>
#include <vector>

#include "PulseStreams.h"


using BPrivate::media::PulseStream;
using BPrivate::media::kAudioInputNode;
using BPrivate::media::kFirstProgramNode;


BMediaRoster* BMediaRoster::sDefaultInstance = NULL;


namespace {

BLocker sRosterLock("media roster");
port_id sNodePort = -1;		// the port every node shows: they're all alive
std::vector<BString> sPrograms;	// node ID - kFirstProgramNode -> program


struct NodeEntry {
	live_node_info info;
	std::vector<media_output> outputs;
};


void
copy_name(char* to, const char* from)
{
	strncpy(to, from, B_MEDIA_NAME_LENGTH - 1);
	to[B_MEDIA_NAME_LENGTH - 1] = '\0';
}


media_node
make_node(media_node_id id, uint64 kind)
{
	media_node node;
	node.node = id;
	node.port = sNodePort;
	node.kind = kind;
	return node;
}


// What can be recorded now. Call with sRosterLock held.
std::vector<NodeEntry>
current_nodes()
{
	std::vector<NodeEntry> nodes;

	NodeEntry input;
	input.info.node = make_node(kAudioInputNode, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	copy_name(input.info.name, "Audio input");
	media_output inputOutput;
	inputOutput.node = input.info.node;
	inputOutput.source = media_source(sNodePort, 0);
	inputOutput.format.type = B_MEDIA_RAW_AUDIO;
	inputOutput.format.u.raw_audio = media_raw_audio_format::wildcard;
	// what BMediaRecorder's Connect(format) asks for when told nothing
	inputOutput.format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	inputOutput.format.u.raw_audio.channel_count = 1;
	inputOutput.format.u.raw_audio.frame_rate = 44100;
	inputOutput.format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
	copy_name(inputOutput.name, "Audio input");
	input.outputs.push_back(inputOutput);
	nodes.push_back(input);

	std::vector<PulseStream> streams;
	BPrivate::media::pulse_list_streams(streams);
	for (const PulseStream& stream : streams) {
		size_t slot = 0;
		while (slot < sPrograms.size() && sPrograms[slot] != stream.program)
			slot++;
		if (slot == sPrograms.size())
			sPrograms.push_back(stream.program);
		const media_node_id id = kFirstProgramNode + media_node_id(slot);

		NodeEntry* entry = NULL;
		for (NodeEntry& node : nodes) {
			if (node.info.node.node == id)
				entry = &node;
		}
		if (entry == NULL) {
			NodeEntry node;
			node.info.node = make_node(id, B_BUFFER_PRODUCER);
			copy_name(node.info.name, stream.program.String());
			nodes.push_back(node);
			entry = &nodes.back();
		}
		media_output output;
		output.node = entry->info.node;
		output.source = media_source(sNodePort, int32(stream.index));
		output.format.type = B_MEDIA_RAW_AUDIO;
		output.format.u.raw_audio = media_raw_audio_format::wildcard;
		output.format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
		output.format.u.raw_audio.channel_count = stream.channels;
		output.format.u.raw_audio.frame_rate = float(stream.rate);
		output.format.u.raw_audio.byte_order = B_MEDIA_HOST_ENDIAN;
		copy_name(output.name, stream.name.Length() > 0 ? stream.name.String() : "Output");
		entry->outputs.push_back(output);
	}
	return nodes;
}


// Haiku's GetLiveNodes names: exact, or a prefix ending in '*'
bool
name_matches(const char* pattern, const char* name)
{
	if (pattern == NULL)
		return true;
	size_t length = strlen(pattern);
	if (length > 0 && pattern[length - 1] == '*')
		return strncmp(pattern, name, length - 1) == 0;
	return strcmp(pattern, name) == 0;
}


status_t
find_node(media_node_id id, NodeEntry& found)
{
	for (const NodeEntry& node : current_nodes()) {
		if (node.info.node.node == id) {
			found = node;
			return B_OK;
		}
	}
	return B_MEDIA_BAD_NODE;
}


status_t
copy_outputs(const media_node& node, media_output* buffer, int32 capacity, int32* found,
	media_type type)
{
	if (found == NULL || (buffer == NULL && capacity > 0))
		return B_BAD_VALUE;
	BAutolock locker(sRosterLock);
	NodeEntry entry;
	status_t status = find_node(node.node, entry);
	*found = 0;
	if (status != B_OK)
		return status;
	for (const media_output& output : entry.outputs) {
		if (type != B_MEDIA_UNKNOWN_TYPE && type != B_MEDIA_RAW_AUDIO)
			continue;
		if (*found < capacity)
			buffer[*found] = output;
		(*found)++;
	}
	if (*found > capacity)
		*found = capacity;
	return B_OK;
}

}	// namespace


BMediaRoster::BMediaRoster()
	:
	BLooper("_BMediaRoster_")
{
}


BMediaRoster::~BMediaRoster()
{
}


BMediaRoster*
BMediaRoster::Roster(status_t* _error)
{
	BAutolock locker(sRosterLock);
	if (sDefaultInstance == NULL) {
		sNodePort = create_port(1, "media nodes");
		sDefaultInstance = new BMediaRoster;
	}
	if (_error != NULL)
		*_error = B_OK;
	return sDefaultInstance;
}


BMediaRoster*
BMediaRoster::CurrentRoster()
{
	return sDefaultInstance;
}


bool
BMediaRoster::IsRunning()
{
	return true;
}


status_t
BMediaRoster::GetAudioInput(media_node* _node)
{
	if (_node == NULL)
		return B_BAD_VALUE;
	BAutolock locker(sRosterLock);
	*_node = make_node(kAudioInputNode, B_BUFFER_PRODUCER | B_PHYSICAL_INPUT);
	return B_OK;
}


status_t
BMediaRoster::GetNodeFor(media_node_id node, media_node* clone)
{
	if (clone == NULL)
		return B_BAD_VALUE;
	BAutolock locker(sRosterLock);
	NodeEntry entry;
	status_t status = find_node(node, entry);
	if (status == B_OK)
		*clone = entry.info.node;
	return status;
}


status_t
BMediaRoster::ReleaseNode(const media_node& node)
{
	(void)node;
	return B_OK;
}


status_t
BMediaRoster::GetLiveNodeInfo(const media_node& node, live_node_info* _liveInfo)
{
	if (_liveInfo == NULL)
		return B_BAD_VALUE;
	BAutolock locker(sRosterLock);
	NodeEntry entry;
	status_t status = find_node(node.node, entry);
	if (status == B_OK)
		*_liveInfo = entry.info;
	return status;
}


status_t
BMediaRoster::GetLiveNodes(live_node_info* _liveNodes, int32* inOutTotalCount,
	const media_format* hasInput, const media_format* hasOutput, const char* name,
	uint64 nodeKinds)
{
	if (_liveNodes == NULL || inOutTotalCount == NULL || *inOutTotalCount <= 0)
		return B_BAD_VALUE;
	const int32 capacity = *inOutTotalCount;
	*inOutTotalCount = 0;
	// nothing here takes input
	if (hasInput != NULL)
		return B_OK;
	if (hasOutput != NULL && hasOutput->type != B_MEDIA_UNKNOWN_TYPE
		&& hasOutput->type != B_MEDIA_RAW_AUDIO)
		return B_OK;
	BAutolock locker(sRosterLock);
	for (const NodeEntry& node : current_nodes()) {
		if ((node.info.node.kind & nodeKinds) != nodeKinds
			|| !name_matches(name, node.info.name))
			continue;
		if (*inOutTotalCount == capacity)
			break;
		_liveNodes[(*inOutTotalCount)++] = node.info;
	}
	return B_OK;
}


status_t
BMediaRoster::GetFreeOutputsFor(const media_node& node, media_output* _freeOutputsBuffer,
	int32 bufferCapacity, int32* _foundCount, media_type filterType)
{
	return copy_outputs(node, _freeOutputsBuffer, bufferCapacity, _foundCount, filterType);
}


status_t
BMediaRoster::GetAllOutputsFor(const media_node& node, media_output* _outputsBuffer,
	int32 bufferCapacity, int32* _foundCount)
{
	return copy_outputs(node, _outputsBuffer, bufferCapacity, _foundCount,
		B_MEDIA_UNKNOWN_TYPE);
}


status_t
BMediaRoster::GetConnectedOutputsFor(const media_node& node,
	media_output* _activeOutputsBuffer, int32 bufferCapacity, int32* _foundCount)
{
	(void)node;
	(void)_activeOutputsBuffer;
	(void)bufferCapacity;
	if (_foundCount == NULL)
		return B_BAD_VALUE;
	*_foundCount = 0;
	return B_OK;
}


void
BMediaRoster::MessageReceived(BMessage* message)
{
	BLooper::MessageReceived(message);
}


bool
BMediaRoster::QuitRequested()
{
	return true;
}


BHandler*
BMediaRoster::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 form, const char* property)
{
	return BLooper::ResolveSpecifier(message, index, specifier, form, property);
}


status_t
BMediaRoster::GetSupportedSuites(BMessage* data)
{
	return BLooper::GetSupportedSuites(data);
}


status_t BMediaRoster::_Reserved_MediaRoster_0(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_1(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_2(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_3(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_4(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_5(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_6(void*) { return B_ERROR; }
status_t BMediaRoster::_Reserved_MediaRoster_7(void*) { return B_ERROR; }
