/*------------------------------------------------------------------------------
//	Copyright (c) 2004-2026, Bill Hayden
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		fs.c
//	Authors:		Bill Hayden (hayden@haydentech.com)
//----------------------------------------------------------------------------*/

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE	// statx()
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dirent.h>

#ifdef __linux__
#include <mntent.h>
#include <sys/statvfs.h>
#ifdef __linux__
#include <sys/vfs.h>
#endif
#elif defined(__APPLE__)
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif

#include <fs_attr.h>
#include <fs_info.h>
#include <fs_index.h>

#include <TypeConstants.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#endif

#include "../../../config.h"

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#else
#if !defined(_WIN32)
#warning Cosmoe does not support attributes on this platform
#endif
#endif


typedef struct attr_type_entry {
	dev_t device;
	ino_t inode;
	char* name;
	uint32 type;
	struct attr_type_entry* next;
} attr_type_entry;


static pthread_mutex_t sAttrTypeLock = PTHREAD_MUTEX_INITIALIZER;
static attr_type_entry* sAttrTypeHead = NULL;

#if defined(HAVE_SYS_XATTR_H) && !defined(_WIN32)
static const char* kAttrTypeMapXattr = "user.COSMOE:ATTR_TYPES";

typedef struct persistent_attr_type_record {
	uint32 nameLength;
	uint32 type;
} persistent_attr_type_record;
#endif

#if defined(_WIN32)

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define COSMOE_ATTR_STREAM_PREFIX "cosmoe.attr."
#define COSMOE_ATTR_TYPE_STREAM_PREFIX "cosmoe.attrtype."

typedef struct attr_dir_wrapper {
	HANDLE handle;
	WIN32_FIND_STREAM_DATA streamData;
	int firstPending;
	int atEnd;
	WCHAR path[MAX_PATH];
	struct dirent entry;
} attr_dir_wrapper;


static int
_GetPathForFD(int fd, char* path, size_t pathSize)
{
	if (fd < 0 || path == NULL || pathSize == 0) {
		errno = EINVAL;
		return 0;
	}

	intptr_t osHandle = _get_osfhandle(fd);
	if (osHandle == -1) {
		errno = EBADF;
		return 0;
	}

	DWORD length = GetFinalPathNameByHandleA((HANDLE)osHandle, path,
		(DWORD)pathSize, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	if (length == 0 || length >= pathSize) {
		errno = ENOENT;
		return 0;
	}

	if (strncmp(path, "\\\\?\\UNC\\", 8) == 0) {
		path[0] = '\\';
		path[1] = '\\';
		memmove(path + 2, path + 8, strlen(path + 8) + 1);
	} else if (strncmp(path, "\\\\?\\", 4) == 0) {
		memmove(path, path + 4, strlen(path + 4) + 1);
	}

	return 1;
}


static char
_HexDigit(unsigned int value)
{
	return (value < 10) ? ('0' + value) : ('a' + value - 10);
}


static char*
_HexEncodeAttributeName(const char* attribute)
{
	size_t length;
	char* encoded;
	size_t i;

	if (attribute == NULL)
		return NULL;

	length = strlen(attribute);
	encoded = (char*)malloc(length * 2 + 1);
	if (encoded == NULL)
		return NULL;

	for (i = 0; i < length; i++) {
		unsigned char value = (unsigned char)attribute[i];
		encoded[i * 2] = _HexDigit((value >> 4) & 0xf);
		encoded[i * 2 + 1] = _HexDigit(value & 0xf);
	}
	encoded[length * 2] = '\0';
	return encoded;
}


static int
_HexNibble(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}


static int
_HexDecodeAttributeName(const char* encoded, char* attribute, size_t attributeSize)
{
	size_t encodedLength;
	size_t decodedLength;
	size_t i;

	if (encoded == NULL || attribute == NULL || attributeSize == 0)
		return 0;

	encodedLength = strlen(encoded);
	if ((encodedLength & 1) != 0)
		return 0;

	decodedLength = encodedLength / 2;
	if (decodedLength >= attributeSize)
		return 0;

	for (i = 0; i < decodedLength; i++) {
		int high = _HexNibble(encoded[i * 2]);
		int low = _HexNibble(encoded[i * 2 + 1]);
		if (high < 0 || low < 0)
			return 0;
		attribute[i] = (char)((high << 4) | low);
	}

	attribute[decodedLength] = '\0';
	return 1;
}


static void
_SetErrnoFromWin32Error(DWORD error)
{
	switch (error) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
		case ERROR_INVALID_NAME:
		case ERROR_HANDLE_EOF:
			errno = B_ENTRY_NOT_FOUND;
			break;
		case ERROR_ACCESS_DENIED:
			errno = B_PERMISSION_DENIED;
			break;
		case ERROR_ALREADY_EXISTS:
		case ERROR_FILE_EXISTS:
			errno = B_FILE_EXISTS;
			break;
		case ERROR_NOT_ENOUGH_MEMORY:
		case ERROR_OUTOFMEMORY:
			errno = B_NO_MEMORY;
			break;
		default:
			errno = B_ERROR;
			break;
	}
}


static char*
_BuildAttrStreamPath(const char* basePath, const char* attribute,
	const char* prefix)
{
	char* encodedName;
	char* result;
	size_t totalLength;

	if (basePath == NULL || attribute == NULL || prefix == NULL)
		return NULL;

	encodedName = _HexEncodeAttributeName(attribute);
	if (encodedName == NULL)
		return NULL;

	totalLength = strlen(basePath) + 1 + strlen(prefix) + strlen(encodedName) + 1;
	result = (char*)malloc(totalLength);
	if (result != NULL)
		snprintf(result, totalLength, "%s:%s%s", basePath, prefix, encodedName);

	free(encodedName);
	return result;
}


static int
_IsDirectoryPath(const char* path, BOOL* isDirectory)
{
	DWORD attributes;

	if (path == NULL) {
		errno = EINVAL;
		return 0;
	}

	attributes = GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		_SetErrnoFromWin32Error(GetLastError());
		return 0;
	}

	if (isDirectory != NULL)
		*isDirectory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	return 1;
}


static int
_OpenADSStreamFD(const char* basePath, const char* attribute,
	const char* prefix, int openMode)
{
	char* streamPath;
	BOOL isDirectory = FALSE;
	DWORD desiredAccess = 0;
	DWORD creationDisposition = OPEN_EXISTING;
	DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
	HANDLE handle;
	int fd;
	int accessMode;

	if (!_IsDirectoryPath(basePath, &isDirectory))
		return -1;

	streamPath = _BuildAttrStreamPath(basePath, attribute, prefix);
	if (streamPath == NULL) {
		errno = B_NO_MEMORY;
		return -1;
	}

	accessMode = openMode & O_ACCMODE;
	if (accessMode == O_WRONLY)
		desiredAccess = GENERIC_WRITE;
	else if (accessMode == O_RDWR)
		desiredAccess = GENERIC_READ | GENERIC_WRITE;
	else
		desiredAccess = GENERIC_READ;

	if ((openMode & O_TRUNC) != 0) {
		creationDisposition = (openMode & O_CREAT) ? CREATE_ALWAYS : TRUNCATE_EXISTING;
	} else if ((openMode & O_CREAT) != 0)
		creationDisposition = OPEN_ALWAYS;

	if (isDirectory)
		flagsAndAttributes |= FILE_FLAG_BACKUP_SEMANTICS;

	handle = CreateFileA(streamPath, desiredAccess,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		creationDisposition, flagsAndAttributes, NULL);
	free(streamPath);
	if (handle == INVALID_HANDLE_VALUE) {
		_SetErrnoFromWin32Error(GetLastError());
		return -1;
	}

	fd = _open_osfhandle((intptr_t)handle, openMode | O_BINARY);
	if (fd < 0) {
		CloseHandle(handle);
		errno = B_ERROR;
		return -1;
	}

	return fd;
}


static int
_WriteAttrTypeFile(const char* basePath, const char* attribute, uint32 type)
{
	int fd;
	ssize_t written;

	fd = _OpenADSStreamFD(basePath, attribute, COSMOE_ATTR_TYPE_STREAM_PREFIX,
		O_CREAT | O_TRUNC | O_WRONLY);
	if (fd < 0)
		return 0;

	written = _write(fd, &type, sizeof(type));
	_close(fd);
	if (written != (ssize_t)sizeof(type)) {
		errno = EIO;
		return 0;
	}

	return 1;
}


static int
_ReadAttrTypeFile(const char* basePath, const char* attribute, uint32* type)
{
	int fd;
	ssize_t bytesRead;
	uint32 storedType;

	fd = _OpenADSStreamFD(basePath, attribute, COSMOE_ATTR_TYPE_STREAM_PREFIX,
		O_RDONLY);
	if (fd < 0)
		return 0;

	bytesRead = _read(fd, &storedType, sizeof(storedType));
	_close(fd);
	if (bytesRead != (ssize_t)sizeof(storedType))
		return 0;

	if (type != NULL)
		*type = storedType;
	return 1;
}


static int
_OpenAttrDataFD(const char* basePath, const char* attribute, int openMode)
{
	return _OpenADSStreamFD(basePath, attribute, COSMOE_ATTR_STREAM_PREFIX,
		openMode);
}


static int
_DeleteAttrStream(const char* basePath, const char* attribute, const char* prefix)
{
	char* streamPath = _BuildAttrStreamPath(basePath, attribute, prefix);
	if (streamPath == NULL) {
		errno = B_NO_MEMORY;
		return 0;
	}

	if (!DeleteFileA(streamPath)) {
		free(streamPath);
		_SetErrnoFromWin32Error(GetLastError());
		return 0;
	}

	free(streamPath);
	return 1;
}


static int
_OpenBasePathFD(const char* path)
{
	DWORD attributes;
	HANDLE handle;
	int fd;

	fd = _open(path, O_RDONLY | O_BINARY);
	if (fd >= 0)
		return fd;

	attributes = GetFileAttributesA(path);
	if (attributes == INVALID_FILE_ATTRIBUTES
		|| (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
		return -1;
	}

	handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return -1;

	fd = _open_osfhandle((intptr_t)handle, O_RDONLY | O_BINARY);
	if (fd < 0)
		CloseHandle(handle);

	return fd;
}

#endif


static int
_GetFDIdentity(int fd, dev_t* device, ino_t* inode)
{
	struct stat st;
	if (fstat(fd, &st) != 0)
		return 0;

	if (device)
		*device = st.st_dev;
	if (inode)
		*inode = st.st_ino;
	return 1;
}


static void
_SetAttrTypeForFD(int fd, const char* attribute, uint32 type)
{
	dev_t device;
	ino_t inode;
	if (!_GetFDIdentity(fd, &device, &inode) || attribute == NULL)
		return;

	pthread_mutex_lock(&sAttrTypeLock);

	for (attr_type_entry* entry = sAttrTypeHead; entry != NULL;
		entry = entry->next) {
		if (entry->device == device && entry->inode == inode
			&& strcmp(entry->name, attribute) == 0) {
			entry->type = type;
			pthread_mutex_unlock(&sAttrTypeLock);
			return;
		}
	}

	attr_type_entry* entry = (attr_type_entry*)malloc(sizeof(attr_type_entry));
	if (entry != NULL) {
		entry->name = strdup(attribute);
		if (entry->name != NULL) {
			entry->device = device;
			entry->inode = inode;
			entry->type = type;
			entry->next = sAttrTypeHead;
			sAttrTypeHead = entry;
		} else
			free(entry);
	}

	pthread_mutex_unlock(&sAttrTypeLock);
}


static int
_GetAttrTypeForFD(int fd, const char* attribute, uint32* type)
{
	dev_t device;
	ino_t inode;
	if (!_GetFDIdentity(fd, &device, &inode) || attribute == NULL)
		return 0;

	pthread_mutex_lock(&sAttrTypeLock);

	for (attr_type_entry* entry = sAttrTypeHead; entry != NULL;
		entry = entry->next) {
		if (entry->device == device && entry->inode == inode
			&& strcmp(entry->name, attribute) == 0) {
			if (type)
				*type = entry->type;
			pthread_mutex_unlock(&sAttrTypeLock);
			return 1;
		}
	}

	pthread_mutex_unlock(&sAttrTypeLock);
	return 0;
}


static void
_RemoveAttrTypeForFD(int fd, const char* attribute)
{
	dev_t device;
	ino_t inode;
	if (!_GetFDIdentity(fd, &device, &inode) || attribute == NULL)
		return;

	pthread_mutex_lock(&sAttrTypeLock);

	attr_type_entry* previous = NULL;
	attr_type_entry* entry = sAttrTypeHead;
	while (entry != NULL) {
		if (entry->device == device && entry->inode == inode
			&& strcmp(entry->name, attribute) == 0) {
			if (previous)
				previous->next = entry->next;
			else
				sAttrTypeHead = entry->next;

			free(entry->name);
			free(entry);
			break;
		}

		previous = entry;
		entry = entry->next;
	}

	pthread_mutex_unlock(&sAttrTypeLock);
}


#if defined(HAVE_SYS_XATTR_H) && !defined(_WIN32)
static int
_ReadPersistentAttrTypeBlob(int fd, char** buffer, size_t* size)
{
	ssize_t blobSize;
	char* blob;

	if (buffer == NULL || size == NULL)
		return 0;

#ifdef __APPLE__
	blobSize = fgetxattr(fd, kAttrTypeMapXattr, NULL, 0, 0, 0);
#else
	blobSize = fgetxattr(fd, kAttrTypeMapXattr, NULL, 0);
#endif
	if (blobSize <= 0)
		return 0;

	blob = (char*)malloc((size_t)blobSize);
	if (blob == NULL)
		return 0;

#ifdef __APPLE__
	blobSize = fgetxattr(fd, kAttrTypeMapXattr, blob, (size_t)blobSize, 0, 0);
#else
	blobSize = fgetxattr(fd, kAttrTypeMapXattr, blob, (size_t)blobSize);
#endif
	if (blobSize <= 0) {
		free(blob);
		return 0;
	}

	*buffer = blob;
	*size = (size_t)blobSize;
	return 1;
}


static int
_LookupPersistentAttrTypeForFD(int fd, const char* attribute, uint32* type)
{
	char* blob = NULL;
	size_t size = 0;
	size_t offset = 0;
	size_t attributeLength;

	if (attribute == NULL || !_ReadPersistentAttrTypeBlob(fd, &blob, &size))
		return 0;

	attributeLength = strlen(attribute);
	while (offset + sizeof(persistent_attr_type_record) <= size) {
		persistent_attr_type_record record;
		const char* name;

		memcpy(&record, blob + offset, sizeof(record));
		offset += sizeof(record);
		if (record.nameLength > size - offset)
			break;

		name = blob + offset;
		if (record.nameLength == attributeLength
			&& memcmp(name, attribute, attributeLength) == 0) {
			if (type != NULL)
				*type = record.type;
			free(blob);
			return 1;
		}

		offset += record.nameLength;
	}

	free(blob);
	return 0;
}


static int
_WritePersistentAttrTypeForFD(int fd, const char* attribute, uint32 type)
{
	char* oldBlob = NULL;
	size_t oldSize = 0;
	size_t offset = 0;
	size_t newSize;
	size_t writeOffset = 0;
	size_t attributeLength;
	char* newBlob;
	persistent_attr_type_record record;

	if (attribute == NULL)
		return 0;

	(void)_ReadPersistentAttrTypeBlob(fd, &oldBlob, &oldSize);
	attributeLength = strlen(attribute);
	newSize = sizeof(record) + attributeLength;

	while (oldBlob != NULL
		&& offset + sizeof(persistent_attr_type_record) <= oldSize) {
		persistent_attr_type_record oldRecord;
		const char* oldName;

		memcpy(&oldRecord, oldBlob + offset, sizeof(oldRecord));
		if (oldRecord.nameLength > oldSize - offset - sizeof(oldRecord)) {
			oldSize = 0;
			break;
		}

		oldName = oldBlob + offset + sizeof(oldRecord);
		if (!(oldRecord.nameLength == attributeLength
			&& memcmp(oldName, attribute, attributeLength) == 0)) {
			newSize += sizeof(oldRecord) + oldRecord.nameLength;
		}

		offset += sizeof(oldRecord) + oldRecord.nameLength;
	}

	newBlob = (char*)malloc(newSize);
	if (newBlob == NULL) {
		free(oldBlob);
		return 0;
	}

	offset = 0;
	while (oldBlob != NULL
		&& offset + sizeof(persistent_attr_type_record) <= oldSize) {
		persistent_attr_type_record oldRecord;
		const char* oldName;

		memcpy(&oldRecord, oldBlob + offset, sizeof(oldRecord));
		oldName = oldBlob + offset + sizeof(oldRecord);
		if (!(oldRecord.nameLength == attributeLength
			&& memcmp(oldName, attribute, attributeLength) == 0)) {
			memcpy(newBlob + writeOffset, &oldRecord, sizeof(oldRecord));
			writeOffset += sizeof(oldRecord);
			memcpy(newBlob + writeOffset, oldName, oldRecord.nameLength);
			writeOffset += oldRecord.nameLength;
		}

		offset += sizeof(oldRecord) + oldRecord.nameLength;
	}

	record.nameLength = (uint32)attributeLength;
	record.type = type;
	memcpy(newBlob + writeOffset, &record, sizeof(record));
	writeOffset += sizeof(record);
	memcpy(newBlob + writeOffset, attribute, attributeLength);

#ifdef __APPLE__
	if (fsetxattr(fd, kAttrTypeMapXattr, newBlob, newSize, 0, 0) != 0) {
#else
	if (fsetxattr(fd, kAttrTypeMapXattr, newBlob, newSize, 0) != 0) {
#endif
		free(oldBlob);
		free(newBlob);
		return 0;
	}

	free(oldBlob);
	free(newBlob);
	return 1;
}


static int
_RemovePersistentAttrTypeForFD(int fd, const char* attribute)
{
	char* oldBlob = NULL;
	size_t oldSize = 0;
	size_t offset = 0;
	size_t newSize = 0;
	size_t writeOffset = 0;
	size_t attributeLength;
	char* newBlob = NULL;
	int found = 0;

	if (attribute == NULL)
		return 0;
	if (!_ReadPersistentAttrTypeBlob(fd, &oldBlob, &oldSize))
		return 1;

	attributeLength = strlen(attribute);
	while (offset + sizeof(persistent_attr_type_record) <= oldSize) {
		persistent_attr_type_record record;
		const char* name;

		memcpy(&record, oldBlob + offset, sizeof(record));
		if (record.nameLength > oldSize - offset - sizeof(record)) {
			free(oldBlob);
			return 0;
		}

		name = oldBlob + offset + sizeof(record);
		if (record.nameLength == attributeLength
			&& memcmp(name, attribute, attributeLength) == 0) {
			found = 1;
		} else {
			newSize += sizeof(record) + record.nameLength;
		}

		offset += sizeof(record) + record.nameLength;
	}

	if (!found) {
		free(oldBlob);
		return 1;
	}

	if (newSize == 0) {
#ifdef __APPLE__
		int result = fremovexattr(fd, kAttrTypeMapXattr, 0) == 0;
#else
		int result = fremovexattr(fd, kAttrTypeMapXattr) == 0;
#endif
		free(oldBlob);
		return result;
	}

	newBlob = (char*)malloc(newSize);
	if (newBlob == NULL) {
		free(oldBlob);
		return 0;
	}

	offset = 0;
	while (offset + sizeof(persistent_attr_type_record) <= oldSize) {
		persistent_attr_type_record record;
		const char* name;

		memcpy(&record, oldBlob + offset, sizeof(record));
		name = oldBlob + offset + sizeof(record);
		if (!(record.nameLength == attributeLength
			&& memcmp(name, attribute, attributeLength) == 0)) {
			memcpy(newBlob + writeOffset, &record, sizeof(record));
			writeOffset += sizeof(record);
			memcpy(newBlob + writeOffset, name, record.nameLength);
			writeOffset += record.nameLength;
		}

		offset += sizeof(record) + record.nameLength;
	}

#ifdef __APPLE__
	if (fsetxattr(fd, kAttrTypeMapXattr, newBlob, newSize, 0, 0) != 0) {
#else
	if (fsetxattr(fd, kAttrTypeMapXattr, newBlob, newSize, 0) != 0) {
#endif
		free(oldBlob);
		free(newBlob);
		return 0;
	}

	free(oldBlob);
	free(newBlob);
	return 1;
}
#endif



ssize_t  read_pos(int fd, off_t pos, void *buffer, size_t count)
{
	off_t origPos = lseek(fd, 0, SEEK_CUR);
	if (origPos < 0)
		return -1;
	
	if (lseek(fd, pos, SEEK_SET) < 0)
		return -1;
	
	ssize_t result = read(fd, buffer, count);
	
	// Restore original position (best effort)
	lseek(fd, origPos, SEEK_SET);
	return result;
}

ssize_t  write_pos(int fd, off_t pos, const void *buffer, size_t count)
{
	off_t origPos = lseek(fd, 0, SEEK_CUR);
	if (origPos < 0)
		return -1;
	
	if (lseek(fd, pos, SEEK_SET) < 0)
		return -1;
	
	ssize_t result = write(fd, buffer, count);
	
	// Restore original position (best effort)
	lseek(fd, origPos, SEEK_SET);
	return result;
}

// We're in a bit of a bind here since dev_t is unsigned on Linux, but
// was signed on BeOS. So we treat -1 as invalid, and everything else as valid.
dev_t dev_for_path(const char *path)
{
	if (!path) {
		return (dev_t)-1;
	}
	struct stat st;
	if (stat(path, &st) != 0) {
		return (dev_t)-1;
	}
	return st.st_dev;
}


#if !defined(_WIN32)

static int
_StartsWith(const char* value, const char* prefix)
{
	if (value == NULL || prefix == NULL)
		return 0;

	while (*prefix != '\0') {
		if (*value++ != *prefix++)
			return 0;
	}

	return 1;
}

#endif


#ifdef __linux__
static int
_IsLinuxPseudoFilesystem(const char* fsType)
{
	static const char* const kPseudoTypes[] = {
		"proc", "procfs", "sysfs", "devtmpfs", "devpts", "tmpfs",
		"mqueue", "cgroup", "cgroup2", "pstore", "securityfs",
		"configfs", "debugfs", "tracefs", "fusectl", "rpc_pipefs",
		"hugetlbfs", "overlay", "nsfs", "ramfs"
	};

	if (fsType == NULL)
		return 1;

	for (size_t i = 0; i < sizeof(kPseudoTypes) / sizeof(kPseudoTypes[0]); i++) {
		if (strcmp(fsType, kPseudoTypes[i]) == 0)
			return 1;
	}

	return 0;
}


static int
_IsLinuxInternalMountPoint(const char* mountPoint)
{
	if (mountPoint == NULL)
		return 1;

	if (_StartsWith(mountPoint, "/proc")
		|| _StartsWith(mountPoint, "/sys")
		|| _StartsWith(mountPoint, "/dev")) {
		return 1;
	}

	if (_StartsWith(mountPoint, "/run")
		&& !_StartsWith(mountPoint, "/run/media")) {
		return 1;
	}

	return 0;
}


static int
_IsUserVisibleLinuxMount(const struct mntent* ent)
{
	if (ent == NULL)
		return 0;

	if (_IsLinuxPseudoFilesystem(ent->mnt_type)
		|| _IsLinuxInternalMountPoint(ent->mnt_dir)) {
		return 0;
	}

	if (!_StartsWith(ent->mnt_fsname, "/dev/"))
		return 0;

	if (_StartsWith(ent->mnt_fsname, "/dev/loop")
		|| _StartsWith(ent->mnt_fsname, "/dev/ram")
		|| _StartsWith(ent->mnt_fsname, "/dev/zram")) {
		return 0;
	}

	return 1;
}
#elif defined(__APPLE__)
static int
_IsUserVisibleMacMount(const struct statfs* mount)
{
	if (mount == NULL)
		return 0;

	if (!_StartsWith(mount->f_mntfromname, "/dev/"))
		return 0;

	if ((mount->f_flags & MNT_DONTBROWSE) != 0)
		return 0;

	if (strcmp(mount->f_fstypename, "devfs") == 0
		|| strcmp(mount->f_fstypename, "autofs") == 0
		|| strcmp(mount->f_fstypename, "procfs") == 0
		|| strcmp(mount->f_fstypename, "fdesc") == 0
		|| strcmp(mount->f_fstypename, "volfs") == 0) {
		return 0;
	}

	return 1;
}
#endif

dev_t next_dev(int32 *pos)
{
	if (!pos || *pos < 0)
		return (dev_t)-1;

#ifdef __linux__
	// Linux-specific: Use /proc/mounts to enumerate mounted filesystems
	FILE* mounts = setmntent("/proc/mounts", "r");
	if (!mounts)
		return (dev_t)-1;

	dev_t result = (dev_t)-1;
	int32 targetIndex = *pos;
	int32 found = 0;

	/* Keep a small list of seen devices to avoid duplicates */
	dev_t seen[256];
	int seenCount = 0;

	struct mntent *ent;
	while ((ent = getmntent(mounts)) != NULL) {
		if (!_IsUserVisibleLinuxMount(ent))
			continue;

		struct stat st;
		if (stat(ent->mnt_dir, &st) != 0)
			continue;

		/* dedupe */
		int alreadySeen = 0;
		for (int i = 0; i < seenCount; i++) {
			if (seen[i] == st.st_dev) {
				alreadySeen = 1;
				break;
			}
		}
		if (alreadySeen)
			continue;
		if (seenCount < (int)(sizeof(seen) / sizeof(seen[0])))
			seen[seenCount++] = st.st_dev;

		if (found == targetIndex) {
			result = st.st_dev;
			*pos = targetIndex + 1; /* advance cookie */
			break;
		}
		found++;
	}

	endmntent(mounts);
	return result;
#elif defined(__APPLE__)
	// macOS: Use getfsstat() to enumerate mounted filesystems
	int numMounts = getfsstat(NULL, 0, MNT_NOWAIT);
	if (numMounts <= 0)
		return (dev_t)-1;
	
	struct statfs *mounts = malloc(sizeof(struct statfs) * numMounts);
	if (!mounts)
		return (dev_t)-1;
	
	numMounts = getfsstat(mounts, sizeof(struct statfs) * numMounts, MNT_NOWAIT);
	if (numMounts <= 0) {
		free(mounts);
		return (dev_t)-1;
	}
	
	dev_t result = (dev_t)-1;
	int32 targetIndex = *pos;
	
	// Keep a small list of seen devices to avoid duplicates
	dev_t seen[256];
	int seenCount = 0;
	int found = 0;
	
	for (int i = 0; i < numMounts; i++) {
		if (!_IsUserVisibleMacMount(&mounts[i]))
			continue;

		struct stat st;
		if (stat(mounts[i].f_mntonname, &st) != 0)
			continue;
		
		// Dedupe
		int alreadySeen = 0;
		for (int j = 0; j < seenCount; j++) {
			if (seen[j] == st.st_dev) {
				alreadySeen = 1;
				break;
			}
		}
		if (alreadySeen)
			continue;
		if (seenCount < (int)(sizeof(seen) / sizeof(seen[0])))
			seen[seenCount++] = st.st_dev;
		
		if (found == targetIndex) {
			result = st.st_dev;
			*pos = targetIndex + 1; // advance cookie
			break;
		}
		found++;
	}
	
	free(mounts);
	return result;
#else
	// TODO: Implement Windows
	return (dev_t)-1;  // Placeholder
#endif
}

#ifdef __linux__
/*! Checks whether the file system mounted at \a mountPoint supports user
	extended attributes. This is a non-mutating probe: file systems that support
	them report ENODATA for a missing attribute, while file systems without
	support (or with user xattr support disabled) report EOPNOTSUPP/ENOTSUP.
	Unreadable mount points are conservatively reported as unsupported.
*/
static int
xattr_supported_on_mount(const char *mountPoint)
{
#if defined(HAVE_SYS_XATTR_H)
	if (getxattr(mountPoint, "user.cosmoe.probe", NULL, 0) >= 0)
		return 1;

	return errno == ENODATA;
#else
	(void)mountPoint;
	return 0;
#endif
}
#endif


int	fs_stat_dev(dev_t dev, fs_info *info)
{
	if (dev == (dev_t)-1 || info == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

#ifdef __linux__
	// Linux-specific: Use /proc/mounts to find filesystem information
	FILE* mounts = setmntent("/proc/mounts", "r");
	if (!mounts) {
		errno = B_BAD_VALUE;
		return -1;
	}

	struct mntent *ent;
	int ret = -1;
	
	while ((ent = getmntent(mounts)) != NULL) {
		struct stat st;
		if (stat(ent->mnt_dir, &st) != 0)
			continue;
		if (st.st_dev != dev)
			continue;

		/* Found matching mount; gather statvfs info */
		struct statvfs vfs;
		if (statvfs(ent->mnt_dir, &vfs) != 0)
			break;

		memset(info, 0, sizeof(*info));
		info->dev = dev;
		info->root = st.st_ino;
		info->block_size = (off_t)vfs.f_bsize;
		info->total_blocks = (off_t)vfs.f_blocks;
		info->free_blocks = (off_t)vfs.f_bfree;

		strncpy(info->device_name, ent->mnt_fsname, sizeof(info->device_name)-1);
		info->device_name[sizeof(info->device_name)-1] = '\0';

		/* Use mount point as volume name if available */
		const char* base = strrchr(ent->mnt_dir, '/');
		if (base && base[1] != '\0')
			strncpy(info->volume_name, base + 1, sizeof(info->volume_name)-1);
		else
			strncpy(info->volume_name, ent->mnt_dir, sizeof(info->volume_name)-1);
		info->volume_name[sizeof(info->volume_name)-1] = '\0';

		strncpy(info->fsh_name, ent->mnt_type, sizeof(info->fsh_name)-1);
		info->fsh_name[sizeof(info->fsh_name)-1] = '\0';

		/* Set flags (basic) */
		info->flags = 0;
		info->flags |= B_FS_IS_PERSISTENT;
		if (vfs.f_flag & ST_RDONLY)
			info->flags |= B_FS_IS_READONLY;

		if (vfs.f_flag & ST_NOSUID)
			; /* not mapped but kept for future */

		/* Attributes are stored as user.* xattrs; a file system that accepts
		   them has what BFS's attributes and queries need. */
		if (xattr_supported_on_mount(ent->mnt_dir))
			info->flags |= B_FS_HAS_ATTR | B_FS_HAS_QUERY;

		ret = 0;
		break;
	}
	endmntent(mounts);
	errno = (ret == 0) ? 0 : B_ENTRY_NOT_FOUND;
	return ret;
#elif defined(__APPLE__)
	// macOS: Use getfsstat() to find filesystem information
	int numMounts = getfsstat(NULL, 0, MNT_NOWAIT);
	if (numMounts <= 0) {
		errno = B_BAD_VALUE;
		return -1;
	}
	
	struct statfs *mounts = malloc(sizeof(struct statfs) * numMounts);
	if (!mounts) {
		errno = B_NO_MEMORY;
		return -1;
	}
	
	numMounts = getfsstat(mounts, sizeof(struct statfs) * numMounts, MNT_NOWAIT);
	if (numMounts <= 0) {
		free(mounts);
		errno = B_BAD_VALUE;
		return -1;
	}
	
	int ret = -1;
	for (int i = 0; i < numMounts; i++) {
		struct stat st;
		if (stat(mounts[i].f_mntonname, &st) != 0)
			continue;
		if (st.st_dev != dev)
			continue;
		
		// Found matching mount
		memset(info, 0, sizeof(*info));
		info->dev = dev;
		info->root = st.st_ino;
		info->block_size = (off_t)mounts[i].f_bsize;
		info->total_blocks = (off_t)mounts[i].f_blocks;
		info->free_blocks = (off_t)mounts[i].f_bfree;
		
		// Device name from f_mntfromname
		strncpy(info->device_name, mounts[i].f_mntfromname, sizeof(info->device_name)-1);
		info->device_name[sizeof(info->device_name)-1] = '\0';
		
		// Volume name from mount point
		const char* base = strrchr(mounts[i].f_mntonname, '/');
		if (base && base[1] != '\0')
			strncpy(info->volume_name, base + 1, sizeof(info->volume_name)-1);
		else
			strncpy(info->volume_name, mounts[i].f_mntonname, sizeof(info->volume_name)-1);
		info->volume_name[sizeof(info->volume_name)-1] = '\0';
		
		// Filesystem type name
		strncpy(info->fsh_name, mounts[i].f_fstypename, sizeof(info->fsh_name)-1);
		info->fsh_name[sizeof(info->fsh_name)-1] = '\0';
		
		// Set flags
		info->flags = 0;
		info->flags |= B_FS_IS_PERSISTENT;
		if (mounts[i].f_flags & MNT_RDONLY)
			info->flags |= B_FS_IS_READONLY;
		if (mounts[i].f_flags & MNT_REMOVABLE)
			info->flags |= B_FS_IS_REMOVABLE;
		
		ret = 0;
		break;
	}
	
	free(mounts);
	errno = (ret == 0) ? 0 : B_ENTRY_NOT_FOUND;
	return ret;
#else
	// TODO: Implement Windows
	errno = B_NOT_SUPPORTED;
	return -1;  // Placeholder
#endif
}


#if defined(HAVE_SYS_XATTR_H) && defined(__linux__)
/*
 * BFS keeps a value of any size, Linux file systems don't: btrfs took about
 * 10 KB of one, though it holds thousands of 1 KB values for a file. So a value
 * that doesn't fit is split: its start is the attribute itself, which is then
 * exactly this size, and the rest is in hidden attributes "COSMOE:MORE:<n>:<name>"
 * for n from 1. A value that fits stays one, as other programs expect.
 *
 * (ext4 limits all attributes of a file together, to about one block, unless it
 * was made with ea_inode. This doesn't help there.)
 */
#define COSMOE_ATTR_SPLIT_SIZE 4096
#define COSMOE_ATTR_MORE_PREFIX "COSMOE:MORE:"
#define COSMOE_XATTR_NAME_MAX 255

static int
_MoreAttrName(char* name, size_t size, const char* attribute, int index)
{
	int length = snprintf(name, size, "user." COSMOE_ATTR_MORE_PREFIX "%d:%s",
		index, attribute);
	return length > 0 && length <= COSMOE_XATTR_NAME_MAX && (size_t)length < size;
}


static int
_HasMoreAttr(int fd, const char* attribute)
{
	char name[COSMOE_XATTR_NAME_MAX + 1];
	return _MoreAttrName(name, sizeof(name), attribute, 1)
		&& fgetxattr(fd, name, NULL, 0) >= 0;
}


/*! The size of a value, split or not. */
static ssize_t
_AttrSize(int fd, const char* attrName, const char* attribute)
{
	ssize_t total = fgetxattr(fd, attrName, NULL, 0);
	if (total != COSMOE_ATTR_SPLIT_SIZE)
		return total;

	char name[COSMOE_XATTR_NAME_MAX + 1];
	for (int index = 1; _MoreAttrName(name, sizeof(name), attribute, index);
			index++) {
		ssize_t size = fgetxattr(fd, name, NULL, 0);
		if (size < 0)
			break;
		total += size;
	}
	return total;
}


/*! A whole value in new memory, for the caller to free. */
static ssize_t
_ReadWholeAttr(int fd, const char* attrName, const char* attribute, char** _data)
{
	ssize_t total = _AttrSize(fd, attrName, attribute);
	if (total < 0)
		return total;

	char* data = (char*)malloc(total > 0 ? total : 1);
	if (data == NULL) {
		errno = ENOMEM;
		return -1;
	}

	ssize_t done = fgetxattr(fd, attrName, data, total);
	char name[COSMOE_XATTR_NAME_MAX + 1];
	for (int index = 1; done >= 0 && done < total
			&& _MoreAttrName(name, sizeof(name), attribute, index); index++) {
		ssize_t size = fgetxattr(fd, name, data + done, total - done);
		done = size >= 0 ? done + size : -1;
	}
	if (done != total) {
		free(data);
		errno = EIO;
		return -1;
	}

	*_data = data;
	return total;
}


static void
_RemoveMoreAttrs(int fd, const char* attribute, int firstIndex)
{
	char name[COSMOE_XATTR_NAME_MAX + 1];
	for (int index = firstIndex; _MoreAttrName(name, sizeof(name), attribute,
			index) && fremovexattr(fd, name) == 0; index++) {
	}
}


/*! The start goes last: until then the attribute is what it was. */
static int
_WriteSplitAttr(int fd, const char* attrName, const char* attribute,
	const char* data, size_t size)
{
	char name[COSMOE_XATTR_NAME_MAX + 1];
	int index = 1;
	for (size_t offset = COSMOE_ATTR_SPLIT_SIZE; offset < size;
			offset += COSMOE_ATTR_SPLIT_SIZE, index++) {
		if (!_MoreAttrName(name, sizeof(name), attribute, index)) {
			errno = ENAMETOOLONG;
			return -1;
		}
		size_t length = size - offset;
		if (length > COSMOE_ATTR_SPLIT_SIZE)
			length = COSMOE_ATTR_SPLIT_SIZE;
		if (fsetxattr(fd, name, data + offset, length, 0) != 0)
			return -1;
	}
	if (fsetxattr(fd, attrName, data, COSMOE_ATTR_SPLIT_SIZE, 0) != 0)
		return -1;

	_RemoveMoreAttrs(fd, attribute, index);
	return 0;
}


/*
 * Where a file system refuses even split values: ext4 keeps about a block of
 * attributes for a file, all together, unless it was made with ea_inode. What
 * doesn't fit goes to a store of the user's, in a file of its own:
 *
 *   ~/.local/share/cosmoe/attr-overflow/<fsid>/<inode>-<birth time>/<name>
 *
 * It starts with the type of the attribute. Going by the inode, it stays with
 * the file when that is renamed or moved inside its file system; a copy made by
 * another program doesn't get it. The birth time tells a file from a later one
 * that was given the same inode, so a file can't get another's attributes; a file
 * system without one can't use the store. The largest values go first, so that
 * small ones, which queries and other programs are likelier to look for, stay on
 * the file. A file with attributes in the store has COSMOE:OVERFLOW, so that one
 * without it costs nothing but that one look, and only for an attribute it lacks.
 */
#define COSMOE_ATTR_OVERFLOW_MARKER "user.COSMOE:OVERFLOW"

static int
_OverflowRoot(char* path, size_t size)
{
	const char* data = getenv("XDG_DATA_HOME");
	int length;
	if (data != NULL && data[0] == '/')
		length = snprintf(path, size, "%s/cosmoe/attr-overflow", data);
	else {
		const char* home = getenv("HOME");
		if (home == NULL || home[0] != '/')
			return 0;
		length = snprintf(path, size, "%s/.local/share/cosmoe/attr-overflow", home);
	}
	return length > 0 && (size_t)length < size;
}


/*! The folder of a file's attributes in the store, from what identifies it. */
static int
_OverflowFolderFor(const struct statfs* fileSystem, const struct statx* file,
	char* path, size_t size)
{
	if ((file->stx_mask & STATX_BTIME) == 0
		|| (file->stx_btime.tv_sec == 0 && file->stx_btime.tv_nsec == 0)) {
		errno = ENOTSUP;
		return 0;
	}

	char root[B_PATH_NAME_LENGTH];
	if (!_OverflowRoot(root, sizeof(root))) {
		errno = ENOENT;
		return 0;
	}

	unsigned long long fsid = 0;
	memcpy(&fsid, &fileSystem->f_fsid,
		sizeof(fsid) < sizeof(fileSystem->f_fsid) ? sizeof(fsid)
			: sizeof(fileSystem->f_fsid));
	int length = snprintf(path, size, "%s/%llx/%llu-%lld-%u", root, fsid,
		(unsigned long long)file->stx_ino,
		(long long)file->stx_btime.tv_sec, (unsigned)file->stx_btime.tv_nsec);
	if (length <= 0 || (size_t)length >= size) {
		errno = ENAMETOOLONG;
		return 0;
	}
	return 1;
}


static int
_OverflowFolder(int fd, char* path, size_t size)
{
	struct statfs fileSystem;
	struct statx file;
	if (fstatfs(fd, &fileSystem) != 0
		|| statx(fd, "", AT_EMPTY_PATH, STATX_BTIME | STATX_INO, &file) != 0) {
		return 0;
	}
	return _OverflowFolderFor(&fileSystem, &file, path, size);
}


/*! An attribute's name as a file's: anything goes in one, but '/', and a '.' first. */
static int
_OverflowFileName(const char* attribute, char* name, size_t size)
{
	size_t length = 0;
	for (const char* c = attribute; *c != '\0'; c++) {
		if (length + 4 > size || length + 4 > 255) {
			errno = ENAMETOOLONG;
			return 0;
		}
		if (*c == '/' || *c == '%' || (*c == '.' && c == attribute)) {
			length += snprintf(name + length, size - length, "%%%02x",
				(unsigned char)*c);
		} else
			name[length++] = *c;
	}
	name[length] = '\0';
	return 1;
}


static void
_AttributeFromOverflowFileName(const char* name, char* attribute, size_t size)
{
	size_t length = 0;
	for (const char* c = name; *c != '\0' && length + 1 < size; c++) {
		unsigned value;
		if (*c == '%' && sscanf(c + 1, "%2x", &value) == 1) {
			attribute[length++] = (char)value;
			c += 2;
		} else
			attribute[length++] = *c;
	}
	attribute[length] = '\0';
}


static int
_OverflowFile(int fd, const char* attribute, char* path, size_t size)
{
	char folder[B_PATH_NAME_LENGTH];
	char name[256];
	if (!_OverflowFolder(fd, folder, sizeof(folder))
		|| !_OverflowFileName(attribute, name, sizeof(name))) {
		return 0;
	}
	int length = snprintf(path, size, "%s/%s", folder, name);
	if (length <= 0 || (size_t)length >= size) {
		errno = ENAMETOOLONG;
		return 0;
	}
	return 1;
}


static int
_MakeFolders(char* path)
{
	for (char* slash = strchr(path + 1, '/'); slash != NULL;
			slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		int result = mkdir(path, 0700);
		int error = errno;
		*slash = '/';
		if (result != 0 && error != EEXIST) {
			errno = error;
			return 0;
		}
	}
	return mkdir(path, 0700) == 0 || errno == EEXIST;
}


/*! Reads a whole file of the store, for the caller to free. */
static ssize_t
_ReadOverflowFile(const char* path, uint32* _type, char** _data)
{
	int file = open(path, O_RDONLY | O_CLOEXEC);
	if (file < 0)
		return -1;

	struct stat status;
	uint32 type = B_RAW_TYPE;
	ssize_t size = -1;
	char* data = NULL;
	if (fstat(file, &status) == 0 && status.st_size >= (off_t)sizeof(type)
		&& read(file, &type, sizeof(type)) == (ssize_t)sizeof(type)) {
		size = status.st_size - sizeof(type);
		data = (char*)malloc(size > 0 ? size : 1);
		if (data == NULL || read(file, data, size) != size) {
			free(data);
			data = NULL;
			size = -1;
			errno = EIO;
		}
	}
	close(file);
	if (size >= 0) {
		if (_type != NULL)
			*_type = type;
		*_data = data;
	}
	return size;
}


/*! Whether the value is in the store: then *_data holds it, for the caller to free. */
static ssize_t
_ReadOverflowAttr(int fd, const char* attribute, uint32* _type, char** _data)
{
	if (fgetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, NULL, 0) < 0) {
		errno = ENODATA;
		return -1;
	}
	char path[B_PATH_NAME_LENGTH * 2];
	if (!_OverflowFile(fd, attribute, path, sizeof(path))) {
		errno = ENODATA;
		return -1;
	}
	ssize_t size = _ReadOverflowFile(path, _type, _data);
	if (size < 0)
		errno = ENODATA;
	return size;
}


/*! Puts a value in the store, replacing the file only when it's complete. */
static int
_WriteOverflowFile(int fd, const char* attribute, uint32 type, const void* data,
	size_t size)
{
	char folder[B_PATH_NAME_LENGTH];
	char path[B_PATH_NAME_LENGTH * 2];
	char temporary[B_PATH_NAME_LENGTH * 2 + 32];
	if (!_OverflowFolder(fd, folder, sizeof(folder))
		|| !_OverflowFile(fd, attribute, path, sizeof(path))
		|| !_MakeFolders(folder)) {
		return 0;
	}
	snprintf(temporary, sizeof(temporary), "%s/.new-%d", folder, (int)getpid());

	int file = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (file < 0)
		return 0;
	int done = write(file, &type, sizeof(type)) == (ssize_t)sizeof(type);
	size_t written = 0;
	while (done && written < size) {
		ssize_t bytes = write(file, (const char*)data + written, size - written);
		if (bytes <= 0)
			done = 0;
		else
			written += bytes;
	}
	int error = errno;
	if (close(file) != 0 && done) {
		done = 0;
		error = errno;
	}
	if (done && rename(temporary, path) != 0) {
		done = 0;
		error = errno;
	}
	if (!done) {
		unlink(temporary);
		errno = error;
	}
	return done;
}


/*! Takes a value out of the store. When the last is gone, so is the mark. */
static int
_RemoveOverflowAttr(int fd, const char* attribute)
{
	if (fgetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, NULL, 0) < 0)
		return 0;

	char path[B_PATH_NAME_LENGTH * 2];
	char folder[B_PATH_NAME_LENGTH];
	if (!_OverflowFile(fd, attribute, path, sizeof(path))
		|| !_OverflowFolder(fd, folder, sizeof(folder))) {
		return 0;
	}
	int removed = unlink(path) == 0;
	if (rmdir(folder) == 0 || errno == ENOENT)
		fremovexattr(fd, COSMOE_ATTR_OVERFLOW_MARKER);
	return removed;
}


/*! Moves the largest value of a file to the store, if it's larger than \a than,
	to make room on the file. \return Whether there is more room now.
*/
static int
_SpillLargestAttr(int fd, const char* except, ssize_t than)
{
	ssize_t listSize = flistxattr(fd, NULL, 0);
	if (listSize <= 0)
		return 0;
	char* list = (char*)malloc(listSize);
	if (list == NULL)
		return 0;
	listSize = flistxattr(fd, list, listSize);

	const char* largest = NULL;
	ssize_t largestSize = than;
	for (ssize_t i = 0; i < listSize; i += strlen(list + i) + 1) {
		const char* name = list + i;
		if (strncmp(name, "user.", 5) != 0 || strncmp(name, "user.COSMOE:", 12) == 0
			|| (except != NULL && strcmp(name + 5, except) == 0)) {
			continue;
		}
		ssize_t size = _AttrSize(fd, name, name + 5);
		if (size > largestSize) {
			largest = name;
			largestSize = size;
		}
	}

	int spilled = 0;
	char* data = NULL;
	if (largest != NULL) {
		uint32 type;
		if (!_LookupPersistentAttrTypeForFD(fd, largest + 5, &type))
			type = B_RAW_TYPE;
		ssize_t size = _ReadWholeAttr(fd, largest, largest + 5, &data);
		if (size >= 0 && _WriteOverflowFile(fd, largest + 5, type, data, size)) {
			fremovexattr(fd, largest);
			_RemoveMoreAttrs(fd, largest + 5, 1);
			// (There is room for this now, if there wasn't.)
			if (fsetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, "1", 1, 0) == 0)
				spilled = 1;
		}
		free(data);
	}
	free(list);
	return spilled;
}


/*! Puts a value that won't fit on the file in the store. */
static int
_StoreAttrValue(int fd, const char* attrName, const char* attribute, uint32 type,
	const void* buffer, size_t size)
{
	// Nothing changes on the file unless the store has the value; and the store
	// is no use without the mark, so that comes first.
	struct statfs fileSystem;
	struct statx file;
	char folder[B_PATH_NAME_LENGTH];
	if (fstatfs(fd, &fileSystem) != 0
		|| statx(fd, "", AT_EMPTY_PATH, STATX_BTIME | STATX_INO, &file) != 0
		|| !_OverflowFolderFor(&fileSystem, &file, folder, sizeof(folder))) {
		errno = ENOSPC;
		return -1;
	}
	const int hadMark = fgetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, NULL, 0) >= 0;
	while (fsetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, "1", 1, 0) != 0) {
		if ((errno != ENOSPC && errno != E2BIG)
			|| !_SpillLargestAttr(fd, attribute, 0)) {
			return -1;
		}
	}
	if (!_WriteOverflowFile(fd, attribute, type, buffer, size)) {
		int error = errno;
		if (!hadMark)
			fremovexattr(fd, COSMOE_ATTR_OVERFLOW_MARKER);
		errno = error;
		return -1;
	}

	fremovexattr(fd, attrName);
	_RemoveMoreAttrs(fd, attribute, 1);
	return 0;
}


/* For load_attr_names_for_fd(): the names of the attributes in the store. */
void
_cosmoe_attr_overflow_names(int fd, void (*add)(const char* name, void* cookie),
	void* cookie)
{
	if (fgetxattr(fd, COSMOE_ATTR_OVERFLOW_MARKER, NULL, 0) < 0)
		return;
	char folder[B_PATH_NAME_LENGTH];
	if (!_OverflowFolder(fd, folder, sizeof(folder)))
		return;
	DIR* directory = opendir(folder);
	if (directory == NULL)
		return;
	struct dirent* entry;
	while ((entry = readdir(directory)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;
		char attribute[B_ATTR_NAME_LENGTH + 1];
		_AttributeFromOverflowFileName(entry->d_name, attribute, sizeof(attribute));
		add(attribute, cookie);
	}
	closedir(directory);
}


/* For queries, which go by path: the start of a value in the store, like the
   value that lgetxattr() would give. */
ssize_t
_cosmoe_attr_overflow_read(const char* filePath, const char* attribute,
	void* buffer, size_t size, uint32* _type)
{
	if (lgetxattr(filePath, COSMOE_ATTR_OVERFLOW_MARKER, NULL, 0) < 0)
		return -1;

	struct statfs fileSystem;
	struct statx file;
	char folder[B_PATH_NAME_LENGTH];
	char name[256];
	char path[B_PATH_NAME_LENGTH * 2];
	if (statfs(filePath, &fileSystem) != 0
		|| statx(AT_FDCWD, filePath, AT_SYMLINK_NOFOLLOW,
			STATX_BTIME | STATX_INO, &file) != 0
		|| !_OverflowFolderFor(&fileSystem, &file, folder, sizeof(folder))
		|| !_OverflowFileName(attribute, name, sizeof(name))) {
		return -1;
	}
	snprintf(path, sizeof(path), "%s/%s", folder, name);

	char* data = NULL;
	ssize_t length = _ReadOverflowFile(path, _type, &data);
	if (length < 0)
		return -1;
	if ((size_t)length > size)
		length = size;
	memcpy(buffer, data, length);
	free(data);
	return length;
}


/* Whether there is a store at all: if not, a query needn't look. */
int
_cosmoe_attr_overflow_exists(void)
{
	char root[B_PATH_NAME_LENGTH];
	struct stat status;
	return _OverflowRoot(root, sizeof(root)) && stat(root, &status) == 0;
}


/*! Writes a value in one piece if it fits. If not, split over several attributes
	if that fits, then making room, and in the store as the last resort. If it can't
	be written at all, the attribute stays what it was, as far as there is room for
	that. \a _inStore says where it went: the store has the type, too.
*/
static int
_WriteAttrValue(int fd, const char* attrName, const char* attribute, uint32 type,
	const void* buffer, size_t size, int* _inStore)
{
	*_inStore = 0;
	if (fsetxattr(fd, attrName, buffer, size, 0) == 0) {
		if (_HasMoreAttr(fd, attribute))
			_RemoveMoreAttrs(fd, attribute, 1);
		_RemoveOverflowAttr(fd, attribute);
		return 0;
	}

	if (errno != E2BIG && errno != ENOSPC)
		return -1;
	int error = errno;

	if (size > COSMOE_ATTR_SPLIT_SIZE) {
		char* old = NULL;
		ssize_t oldSize = _ReadWholeAttr(fd, attrName, attribute, &old);
		if (_WriteSplitAttr(fd, attrName, attribute, (const char*)buffer, size)
				== 0) {
			free(old);
			_RemoveOverflowAttr(fd, attribute);
			return 0;
		}
		error = errno == ENAMETOOLONG ? ENOSPC : errno;
		if (oldSize >= 0) {
			if (oldSize <= COSMOE_ATTR_SPLIT_SIZE) {
				_RemoveMoreAttrs(fd, attribute, 1);
				fsetxattr(fd, attrName, old, oldSize, 0);
			} else
				_WriteSplitAttr(fd, attrName, attribute, old, oldSize);
			free(old);
		} else
			_RemoveMoreAttrs(fd, attribute, 1);
	}

	// A value larger than this one can make room for it.
	while (_SpillLargestAttr(fd, attribute, size)) {
		if (fsetxattr(fd, attrName, buffer, size, 0) == 0) {
			if (_HasMoreAttr(fd, attribute))
				_RemoveMoreAttrs(fd, attribute, 1);
			_RemoveOverflowAttr(fd, attribute);
			return 0;
		}
	}

	if (_StoreAttrValue(fd, attrName, attribute, type, buffer, size) == 0) {
		*_inStore = 1;
		return 0;
	}
	// (What is wrong is that it doesn't fit: not whatever the store's trouble is.)
	errno = error;
	return -1;
}


/*! Why an attribute could not be written, as a BeBook error. */
static int
_AttrWriteError(int error)
{
	switch (error) {
		case E2BIG:
		case ENOSPC:
		case EDQUOT:
			return B_DEVICE_FULL;
		case ENAMETOOLONG:
			return B_NAME_TOO_LONG;
		case EACCES:
		case EPERM:
			return B_PERMISSION_DENIED;
		case EROFS:
			return B_READ_ONLY_DEVICE;
		case ENOTSUP:
			return B_NOT_SUPPORTED;
		case ENOMEM:
			return B_NO_MEMORY;
		default:
			return B_BAD_VALUE;
	}
}
#endif	// HAVE_SYS_XATTR_H && __linux__


ssize_t	fs_write_attr(int fd, const char *attribute, uint32 type, off_t pos, const void *buffer, size_t writeBytes)
{
	if (!attribute) {
		errno = B_BAD_VALUE;
		return -1;
	}
	
	if (strlen(attribute) > B_ATTR_NAME_LENGTH) {
		// Setting errno a B_ value intentionally to match BeBook API
		errno = B_NAME_TOO_LONG;
		return -1;
	}

#if defined(HAVE_SYS_XATTR_H)
	char attrName[B_ATTR_NAME_LENGTH];
	snprintf(attrName, sizeof(attrName), "user.%s", attribute);
#ifdef __APPLE__
	int err = fsetxattr(fd, attrName, buffer, writeBytes, 0, 0);  // macOS: position, options
#else
	int inStore = 0;
	int err = _WriteAttrValue(fd, attrName, attribute, type, buffer, writeBytes,
		&inStore);
#endif
	if (err != 0) {
		// Setting errno a B_ value intentionally to match BeBook API
#ifdef __APPLE__
		errno = B_BAD_VALUE;
#else
		errno = _AttrWriteError(errno);
#endif
		return (ssize_t)-1;
	}
#ifndef __APPLE__
	// The type takes room, too.
	while (!inStore && !_WritePersistentAttrTypeForFD(fd, attribute, type)) {
		if (!_SpillLargestAttr(fd, attribute, 0)) {
			errno = B_ERROR;
			return (ssize_t)-1;
		}
	}
#else
	if (!_WritePersistentAttrTypeForFD(fd, attribute, type)) {
		errno = B_ERROR;
		return (ssize_t)-1;
	}
#endif

	_SetAttrTypeForFD(fd, attribute, type);
	
	errno = 0;
	return (ssize_t)writeBytes;
#elif defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];
	int attrFD;
	ssize_t result;

	if (buffer == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return -1;
	}

	attrFD = _OpenAttrDataFD(basePath, attribute, O_CREAT | O_RDWR);
	if (attrFD < 0)
		return -1;

	if (pos == 0 && _chsize_s(attrFD, 0) != 0) {
		_close(attrFD);
		return -1;
	}

	if (_lseeki64(attrFD, pos, SEEK_SET) == -1) {
		_close(attrFD);
		return -1;
	}

	result = _write(attrFD, buffer, (unsigned int)writeBytes);
	_close(attrFD);
	if (result < 0)
		return -1;

	if (!_WriteAttrTypeFile(basePath, attribute, type)) {
		errno = B_ERROR;
		return -1;
	}

	_SetAttrTypeForFD(fd, attribute, type);
	errno = 0;
	return result;
#else
	printf( "Cosmoe: fs_write_attr UNSUPPORTED since xattr support was not compiled in\n" );
	errno = B_ERROR;
	return -1;
#endif
}


ssize_t	fs_read_attr(int fd, const char *attribute, uint32 type, off_t pos, void *buffer, size_t readBytes)
{
	if (!attribute) {
		errno = B_BAD_VALUE;
		return -1;
	}
	
	if (strlen(attribute) > B_ATTR_NAME_LENGTH) {
		// Setting errno a B_ value intentionally to match BeBook API
		errno = B_NAME_TOO_LONG;
		return -1;
	}

#if defined(HAVE_SYS_XATTR_H)
	char attrName[B_ATTR_NAME_LENGTH];
	snprintf(attrName, sizeof(attrName), "user.%s", attribute);

#ifdef __APPLE__
	ssize_t err = fgetxattr(fd, attrName, buffer, readBytes, 0, 0);  // macOS
#else
	// As on Haiku, a read can be of a part of a value. (Most are of all of it,
	// and that takes one call.)
	ssize_t err = pos == 0 ? fgetxattr(fd, attrName, buffer, readBytes) : 0;
	if (pos != 0 || (err < 0 && (errno == ERANGE || errno == ENODATA))
		|| (err == COSMOE_ATTR_SPLIT_SIZE && _HasMoreAttr(fd, attribute))) {
		char* whole = NULL;
		ssize_t size = _ReadWholeAttr(fd, attrName, attribute, &whole);
		if (size < 0 && errno == ENODATA)
			size = _ReadOverflowAttr(fd, attribute, NULL, &whole);
		if (size >= 0) {
			err = 0;
			if (pos >= 0 && pos < size) {
				err = size - pos;
				if ((size_t)err > readBytes)
					err = readBytes;
				memcpy(buffer, whole + pos, err);
			}
			free(whole);
		} else
			err = -1;
	}
#endif
	if (err < 0) {
		// Setting errno a B_ value intentionally to match BeBook API
		errno = B_ENTRY_NOT_FOUND;
		return (ssize_t)-1;
	}

	errno = 0;
	return err;
#elif defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];
	int attrFD;
	ssize_t result;

	if (buffer == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return -1;
	}

	attrFD = _OpenAttrDataFD(basePath, attribute, O_RDONLY);
	if (attrFD < 0) {
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

	if (_lseeki64(attrFD, pos, SEEK_SET) == -1) {
		_close(attrFD);
		return -1;
	}

	result = _read(attrFD, buffer, (unsigned int)readBytes);
	_close(attrFD);
	if (result < 0)
		return -1;

	errno = 0;
	return result;
#else
	printf( "Cosmoe: fs_read_attr UNSUPPORTED since xattr support was not compiled in\n" );
	errno = B_ERROR;
	return -1;
#endif
}


int	fs_remove_attr(int fd, const char *attribute)
{
	if (!attribute) {
		errno = B_BAD_VALUE;
		return -1;
	}
	
	if (strlen(attribute) > B_ATTR_NAME_LENGTH) {
		// Setting errno a B_ value intentionally to match BeBook API
		errno = B_NAME_TOO_LONG;
		return -1;
	}

#if defined(HAVE_SYS_XATTR_H)
	char attrName[B_ATTR_NAME_LENGTH];
	snprintf(attrName, sizeof(attrName), "user.%s", attribute);
#ifdef __APPLE__
	int err = fremovexattr(fd, attrName, 0);  // macOS: options
#else
	int err = fremovexattr(fd, attrName);     // Linux
#endif
#ifndef __APPLE__
	if (err < 0 && _RemoveOverflowAttr(fd, attribute))
		err = 0;
	else if (err == 0)
		_RemoveOverflowAttr(fd, attribute);
#endif
	if (err < 0) {
		// Setting errno a B_ value intentionally to match BeBook API
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

#ifndef __APPLE__
	_RemoveMoreAttrs(fd, attribute, 1);
#endif

	(void)_RemovePersistentAttrTypeForFD(fd, attribute);

	_RemoveAttrTypeForFD(fd, attribute);

	errno = 0;
	return B_OK;
#elif defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];
	int removed = 0;

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return -1;
	}

	if (_DeleteAttrStream(basePath, attribute, COSMOE_ATTR_STREAM_PREFIX))
		removed = 1;
	if (_DeleteAttrStream(basePath, attribute, COSMOE_ATTR_TYPE_STREAM_PREFIX))
		removed = 1;

	if (!removed) {
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

	_RemoveAttrTypeForFD(fd, attribute);
	errno = 0;
	return B_OK;
#else
	printf( "Cosmoe: fs_remove_attr UNSUPPORTED since xattr support was not compiled in\n" );
	errno = B_ERROR;
	return -1;
#endif
}


int	fs_stat_attr(int fd, const char *attribute, struct attr_info *attrInfo)
{
	if (!attribute) {
		errno = B_BAD_VALUE;
		return -1;
	}
	
#if defined(HAVE_SYS_XATTR_H)
	char attrName[B_ATTR_NAME_LENGTH];
	snprintf(attrName, sizeof(attrName), "user.%s", attribute);

#ifdef __APPLE__
	int size = fgetxattr(fd, attrName, NULL, 0, 0, 0);  // macOS
#else
	ssize_t size = _AttrSize(fd, attrName, attribute);
	if (size < 0) {
		uint32 storedType;
		char* whole = NULL;
		ssize_t stored = _ReadOverflowAttr(fd, attribute, &storedType, &whole);
		if (stored >= 0) {
			free(whole);
			if (attrInfo != NULL) {
				attrInfo->size = stored;
				attrInfo->type = storedType;
			}
			return B_OK;
		}
	}
#endif

	if (size < 0)
	{
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

	if (attrInfo) {
		attrInfo->size = size;
		if (!_GetAttrTypeForFD(fd, attribute, &attrInfo->type)
			&& !_LookupPersistentAttrTypeForFD(fd, attribute, &attrInfo->type))
			attrInfo->type = B_RAW_TYPE;
		else
			_SetAttrTypeForFD(fd, attribute, attrInfo->type);
	}

	return B_OK;
#elif defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];
	int attrFD;
	struct _stat64 st;

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return -1;
	}

	attrFD = _OpenAttrDataFD(basePath, attribute, O_RDONLY);
	if (attrFD < 0) {
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

	if (_fstat64(attrFD, &st) != 0) {
		_close(attrFD);
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}
	_close(attrFD);

	if (attrInfo != NULL) {
		attrInfo->size = st.st_size;
		if (!_ReadAttrTypeFile(basePath, attribute, &attrInfo->type)
			&& !_GetAttrTypeForFD(fd, attribute, &attrInfo->type)) {
			attrInfo->type = B_RAW_TYPE;
		}
	}

	errno = 0;
	return B_OK;
#else
	printf( "Cosmoe: fs_stat_attr UNSUPPORTED since xattr support was not compiled in\n" );
	errno = B_ERROR;
	return -1;
#endif
}


int
fs_open_attr(const char *path, const char *attribute, uint32 type, int openMode)
{
#if defined(_WIN32)
	int fileFD;
	int attrFD;

	if (path == NULL || attribute == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

	fileFD = _OpenBasePathFD(path);
	if (fileFD < 0)
		return -1;

	attrFD = fs_fopen_attr(fileFD, attribute, type, openMode);
	_close(fileFD);
	return attrFD;
#else
	(void)path;
	(void)attribute;
	(void)type;
	(void)openMode;
	errno = B_NOT_SUPPORTED;
	return -1;
#endif
}


int
fs_fopen_attr(int fd, const char *attribute, uint32 type, int openMode)
{
#if defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];
	int attrFD;
	int accessMode;

	if (attribute == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return -1;
	}

	accessMode = openMode & O_ACCMODE;
	if (accessMode == O_WRONLY || accessMode == O_RDWR || (openMode & O_CREAT)) {
		if (!_WriteAttrTypeFile(basePath, attribute, type)) {
			errno = B_ERROR;
			return -1;
		}
		_SetAttrTypeForFD(fd, attribute, type);
	}

	attrFD = _OpenAttrDataFD(basePath, attribute, openMode);
	if (attrFD < 0 && (openMode & O_CREAT))
		attrFD = _OpenAttrDataFD(basePath, attribute, openMode | O_CREAT);

	return attrFD;
#else
	(void)fd;
	(void)attribute;
	(void)type;
	(void)openMode;
	errno = B_NOT_SUPPORTED;
	return -1;
#endif
}


int
fs_close_attr(int fd)
{
	return close(fd);
}


/* There are no indices to keep up, but everything that an index is for can be
   asked of a volume that knows queries (see QueryEngine.cpp): so far as a program
   can tell, it has every index already. */
static int
_VolumeHasIndices(dev_t device)
{
	fs_info info;
	return fs_stat_dev(device, &info) == 0 && (info.flags & B_FS_HAS_QUERY) != 0;
}


int
fs_create_index(dev_t device, const char *name, uint32 type, uint32 flags)
{
	(void)type;
	(void)flags;
	if (name == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}
	errno = _VolumeHasIndices(device) ? B_FILE_EXISTS : B_NOT_SUPPORTED;
	return -1;
}


int
fs_remove_index(dev_t device, const char *name)
{
	(void)device;
	(void)name;
	errno = B_NOT_SUPPORTED;
	return -1;
}


int
fs_stat_index(dev_t device, const char *name, void *indexInfo)
{
	if (name == NULL || indexInfo == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}
	if (!_VolumeHasIndices(device)) {
		errno = B_ENTRY_NOT_FOUND;
		return -1;
	}

	/* What an index would say of itself, as far as there is anything to say. */
	index_info* info = (index_info*)indexInfo;
	memset(info, 0, sizeof(*info));
	info->type = B_STRING_TYPE;
	info->uid = getuid();
	info->gid = getgid();
	errno = 0;
	return 0;
}


DIR*
fs_open_index_dir(dev_t device)
{
	(void)device;
	errno = B_NOT_SUPPORTED;
	return NULL;
}


int
fs_close_index_dir(DIR *indexDirectory)
{
	(void)indexDirectory;
	errno = B_NOT_SUPPORTED;
	return -1;
}


struct dirent*
fs_read_index_dir(DIR *indexDirectory)
{
	(void)indexDirectory;
	errno = B_NOT_SUPPORTED;
	return NULL;
}


void
fs_rewind_index_dir(DIR *indexDirectory)
{
	(void)indexDirectory;
}


DIR*
fs_open_attr_dir(const char *path)
{
#if defined(_WIN32)
	attr_dir_wrapper* wrapper;
	int charsNeeded;

	if (path == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	charsNeeded = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	if (charsNeeded <= 0 || charsNeeded > MAX_PATH) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	wrapper = (attr_dir_wrapper*)calloc(1, sizeof(attr_dir_wrapper));
	if (wrapper == NULL) {
		errno = B_NO_MEMORY;
		return NULL;
	}

	MultiByteToWideChar(CP_UTF8, 0, path, -1, wrapper->path, MAX_PATH);
	wrapper->handle = INVALID_HANDLE_VALUE;
	wrapper->firstPending = 0;
	wrapper->atEnd = 0;
	return (DIR*)wrapper;
#else
	(void)path;
	errno = B_NOT_SUPPORTED;
	return NULL;
#endif
}


DIR*
fs_fopen_attr_dir(int fd)
{
#if defined(_WIN32)
	char basePath[B_PATH_NAME_LENGTH];

	if (!_GetPathForFD(fd, basePath, sizeof(basePath))) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	return fs_open_attr_dir(basePath);
#else
	(void)fd;
	errno = B_NOT_SUPPORTED;
	return NULL;
#endif
}


int
fs_close_attr_dir(DIR *dir)
{
#if defined(_WIN32)
	attr_dir_wrapper* wrapper = (attr_dir_wrapper*)dir;

	if (wrapper == NULL) {
		errno = B_BAD_VALUE;
		return -1;
	}

	if (wrapper->handle != INVALID_HANDLE_VALUE)
		FindClose(wrapper->handle);
	free(wrapper);
	return 0;
#else
	(void)dir;
	errno = B_NOT_SUPPORTED;
	return -1;
#endif
}


struct dirent*
fs_read_attr_dir(DIR *dir)
{
#if defined(_WIN32)
	attr_dir_wrapper* wrapper = (attr_dir_wrapper*)dir;
	char streamName[256];
	char* prefixPos;
	char* endPos;
	char decoded[B_ATTR_NAME_LENGTH + 1];

	if (wrapper == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	for (;;) {
		if (wrapper->atEnd)
			return NULL;

		if (!wrapper->firstPending) {
			wrapper->handle = FindFirstStreamW(wrapper->path,
				FindStreamInfoStandard, &wrapper->streamData, 0);
			if (wrapper->handle == INVALID_HANDLE_VALUE) {
				wrapper->atEnd = 1;
				return NULL;
			}
			wrapper->firstPending = 1;
		} else if (wrapper->handle == INVALID_HANDLE_VALUE
			|| !FindNextStreamW(wrapper->handle, &wrapper->streamData)) {
			if (wrapper->handle != INVALID_HANDLE_VALUE)
				FindClose(wrapper->handle);
			wrapper->handle = INVALID_HANDLE_VALUE;
			wrapper->atEnd = 1;
			return NULL;
		}

		if (WideCharToMultiByte(CP_UTF8, 0, wrapper->streamData.cStreamName, -1,
				streamName, sizeof(streamName), NULL, NULL) <= 0) {
			continue;
		}

		prefixPos = strstr(streamName, COSMOE_ATTR_STREAM_PREFIX);
		if (prefixPos == NULL || prefixPos != streamName + 1)
			continue;

		endPos = strstr(prefixPos, ":$DATA");
		if (endPos == NULL)
			continue;
		*endPos = '\0';
		if (!_HexDecodeAttributeName(prefixPos + strlen(COSMOE_ATTR_STREAM_PREFIX),
				decoded, sizeof(decoded))) {
			continue;
		}

		memset(&wrapper->entry, 0, sizeof(wrapper->entry));
		strncpy(wrapper->entry.d_name, decoded, sizeof(wrapper->entry.d_name) - 1);
		wrapper->entry.d_name[sizeof(wrapper->entry.d_name) - 1] = '\0';
		wrapper->entry.d_reclen = sizeof(struct dirent);
		return &wrapper->entry;
	}
#else
	(void)dir;
	errno = B_NOT_SUPPORTED;
	return NULL;
#endif
}


void
fs_rewind_attr_dir(DIR *dir)
{
#if defined(_WIN32)
	attr_dir_wrapper* wrapper = (attr_dir_wrapper*)dir;
	if (wrapper != NULL) {
		if (wrapper->handle != INVALID_HANDLE_VALUE)
			FindClose(wrapper->handle);
		wrapper->handle = INVALID_HANDLE_VALUE;
		wrapper->firstPending = 0;
		wrapper->atEnd = 0;
	}
#else
	(void)dir;
#endif
}


