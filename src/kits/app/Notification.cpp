/*
 * Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *		Stephan Aßmus, superstippi@gmx.de
 *		Brian Hill, supernova@tycho.email
 */


#include <Notification.h>

#include <new>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#	include <sys/wait.h>
#	include <unistd.h>
#endif

#include <notification/Notifications.h>

#include <Application.h>
#include <Bitmap.h>
#include <Message.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>



namespace BPrivate {

struct notification_action {
	BString		label;
	BMessage*	message;
	BMessenger	target;
};

// The actions of a BNotification (a Hokku extension), kept apart from the
// class so its size and layout stay as they were.
struct notification_actions : std::vector<notification_action> {
};

}	// namespace BPrivate


BNotification::BNotification(notification_type type)
	:
	BArchivable(),
	fInitStatus(B_OK),
	fType(type),
	fProgress(0.f),
	fFile(NULL),
	fBitmap(NULL),
	fActions(NULL)
{
	team_info teamInfo;
	get_team_info(B_CURRENT_TEAM, &teamInfo);
	app_info appInfo;
	be_roster->GetRunningAppInfo(teamInfo.team, &appInfo);

	int32 iconSize = B_LARGE_ICON;
	fBitmap = new BBitmap(BRect(0, 0, iconSize - 1, iconSize - 1), 0, B_RGBA32);
	if (fBitmap) {
		if (BNodeInfo::GetTrackerIcon(&appInfo.ref, fBitmap,
			icon_size(iconSize)) != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
		}
	}
	fSourceSignature = appInfo.signature;
	BPath path(&appInfo.ref);
	if (path.InitCheck() == B_OK)
		fSourceName = path.Leaf();
}


BNotification::BNotification(BMessage* archive)
	:
	BArchivable(archive),
	fInitStatus(B_OK),
	fProgress(0.0f),
	fFile(NULL),
	fBitmap(NULL),
	fActions(NULL)
{
	BString appName;
	if (archive->FindString("_appname", &appName) == B_OK)
		fSourceName = appName;

	BString signature;
	if (archive->FindString("_signature", &signature) == B_OK)
		fSourceSignature = signature;

	int32 type;
	if (archive->FindInt32("_type", &type) == B_OK)
		fType = (notification_type)type;
	else
		fInitStatus = B_ERROR;

	BString group;
	if (archive->FindString("_group", &group) == B_OK)
		SetGroup(group);

	BString title;
	if (archive->FindString("_title", &title) == B_OK)
		SetTitle(title);

	BString content;
	if (archive->FindString("_content", &content) == B_OK)
		SetContent(content);

	BString messageID;
	if (archive->FindString("_messageID", &messageID) == B_OK)
		SetMessageID(messageID);

	float progress;
	if (type == B_PROGRESS_NOTIFICATION
		&& archive->FindFloat("_progress", &progress) == B_OK)
		SetProgress(progress);

	BString onClickApp;
	if (archive->FindString("_onClickApp", &onClickApp) == B_OK)
		SetOnClickApp(onClickApp);

	entry_ref onClickFile;
	if (archive->FindRef("_onClickFile", &onClickFile) == B_OK)
		SetOnClickFile(&onClickFile);

	entry_ref onClickRef;
	int32 index = 0;
	while (archive->FindRef("_onClickRef", index++, &onClickRef) == B_OK)
		AddOnClickRef(&onClickRef);

	BString onClickArgv;
	index = 0;
	while (archive->FindString("_onClickArgv", index++, &onClickArgv) == B_OK)
		AddOnClickArg(onClickArgv);

	const char* actionLabel;
	for (index = 0; archive->FindString("_actionLabel", index, &actionLabel)
			== B_OK; index++) {
		BMessage actionMessage;
		BMessenger actionTarget;
		archive->FindMessage("_actionMessage", index, &actionMessage);
		archive->FindMessenger("_actionTarget", index, &actionTarget);
		AddAction(actionLabel, new BMessage(actionMessage), actionTarget);
	}

	status_t ret = B_OK;
	BMessage icon;
	if ((ret = archive->FindMessage("_icon", &icon)) == B_OK) {
		BBitmap bitmap(&icon);
		ret = bitmap.InitCheck();
		if (ret == B_OK)
			ret = SetIcon(&bitmap);
	}
}


BNotification::~BNotification()
{
	delete fFile;
	delete fBitmap;
	if (fActions != NULL) {
		for (size_t i = 0; i < fActions->size(); i++)
			delete (*fActions)[i].message;
		delete fActions;
	}

	for (int32 i = fRefs.CountItems() - 1; i >= 0; i--)
		delete (entry_ref*)fRefs.ItemAtFast(i);

	for (int32 i = fArgv.CountItems() - 1; i >= 0; i--)
		free(fArgv.ItemAtFast(i));
}


status_t
BNotification::InitCheck() const
{
	return fInitStatus;
}


BArchivable*
BNotification::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BNotification"))
		return new(std::nothrow) BNotification(archive);

	return NULL;
}


status_t
BNotification::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);

	if (status == B_OK)
		status = archive->AddString("_appname", fSourceName);

	if (status == B_OK)
		status = archive->AddString("_signature", fSourceSignature);

	if (status == B_OK)
		status = archive->AddInt32("_type", (int32)fType);

	if (status == B_OK && Group() != NULL)
		status = archive->AddString("_group", Group());

	if (status == B_OK && Title() != NULL)
		status = archive->AddString("_title", Title());

	if (status == B_OK && Content() != NULL)
		status = archive->AddString("_content", Content());

	if (status == B_OK && MessageID() != NULL)
		status = archive->AddString("_messageID", MessageID());

	if (status == B_OK && Type() == B_PROGRESS_NOTIFICATION)
		status = archive->AddFloat("_progress", Progress());

	if (status == B_OK && OnClickApp() != NULL)
		status = archive->AddString("_onClickApp", OnClickApp());

	if (status == B_OK && OnClickFile() != NULL)
		status = archive->AddRef("_onClickFile", OnClickFile());

	if (status == B_OK) {
		for (int32 i = 0; i < CountOnClickRefs(); i++) {
			status = archive->AddRef("_onClickRef", OnClickRefAt(i));
			if (status != B_OK)
				break;
		}
	}

	if (status == B_OK) {
		for (int32 i = 0; i < CountOnClickArgs(); i++) {
			status = archive->AddString("_onClickArgv", OnClickArgAt(i));
			if (status != B_OK)
				break;
		}
	}

	if (status == B_OK) {
		const BBitmap* icon = Icon();
		if (icon != NULL) {
			BMessage iconArchive;
			status = icon->Archive(&iconArchive);
			if (status == B_OK)
				archive->AddMessage("_icon", &iconArchive);
		}
	}

	for (int32 i = 0; status == B_OK && i < CountActions(); i++) {
		status = archive->AddString("_actionLabel", ActionLabelAt(i));
		if (status == B_OK)
			status = archive->AddMessage("_actionMessage", ActionMessageAt(i));
		if (status == B_OK)
			status = archive->AddMessenger("_actionTarget", ActionTargetAt(i));
	}

	return status;
}


const char*
BNotification::SourceSignature() const
{
	return fSourceSignature;
}


const char*
BNotification::SourceName() const
{
	return fSourceName;
}


notification_type
BNotification::Type() const
{
	return fType;
}


const char*
BNotification::Group() const
{
	if (fGroup == "")
		return NULL;
	return fGroup;
}


void
BNotification::SetGroup(const BString& group)
{
	fGroup = group;
}


const char*
BNotification::Title() const
{
	if (fTitle == "")
		return NULL;
	return fTitle;
}


void
BNotification::SetTitle(const BString& title)
{
	fTitle = title;
}


const char*
BNotification::Content() const
{
	if (fContent == "")
		return NULL;
	return fContent;
}


void
BNotification::SetContent(const BString& content)
{
	fContent = content;
}


const char*
BNotification::MessageID() const
{
	if (fID == "")
		return NULL;
	return fID;
}


void
BNotification::SetMessageID(const BString& id)
{
	fID = id;
}


float
BNotification::Progress() const
{
	if (fType != B_PROGRESS_NOTIFICATION)
		return -1;
	return fProgress;
}


void
BNotification::SetProgress(float progress)
{
	if (progress < 0)
		fProgress = 0;
	else if (progress > 1)
		fProgress = 1;
	else
		fProgress = progress;
}


const char*
BNotification::OnClickApp() const
{
	if (fApp == "")
		return NULL;
	return fApp;
}


void
BNotification::SetOnClickApp(const BString& app)
{
	fApp = app;
}


const entry_ref*
BNotification::OnClickFile() const
{
	return fFile;
}


status_t
BNotification::SetOnClickFile(const entry_ref* file)
{
	delete fFile;

	if (file != NULL) {
		fFile = new(std::nothrow) entry_ref(*file);
		if (fFile == NULL)
			return B_NO_MEMORY;
	} else
		fFile = NULL;

	return B_OK;
}


status_t
BNotification::AddOnClickRef(const entry_ref* ref)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	entry_ref* clonedRef = new(std::nothrow) entry_ref(*ref);
	if (clonedRef == NULL || !fRefs.AddItem(clonedRef))
		return B_NO_MEMORY;

	return B_OK;
}


int32
BNotification::CountOnClickRefs() const
{
	return fRefs.CountItems();
}


const entry_ref*
BNotification::OnClickRefAt(int32 index) const
{
	return (entry_ref*)fRefs.ItemAt(index);
}


status_t
BNotification::AddOnClickArg(const BString& arg)
{
	char* clonedArg = strdup(arg.String());
	if (clonedArg == NULL || !fArgv.AddItem(clonedArg))
		return B_NO_MEMORY;

	return B_OK;
}


int32
BNotification::CountOnClickArgs() const
{
	return fArgv.CountItems();
}


const char*
BNotification::OnClickArgAt(int32 index) const
{
	return (char*)fArgv.ItemAt(index);
}


const BBitmap*
BNotification::Icon() const
{
	return fBitmap;
}


status_t
BNotification::SetIcon(const BBitmap* icon)
{
	delete fBitmap;

	if (icon != NULL) {
		fBitmap = new(std::nothrow) BBitmap(icon);
		if (fBitmap == NULL)
			return B_NO_MEMORY;
		return fBitmap->InitCheck();
	}

	fBitmap = NULL;
	return B_OK;
}


#ifndef _WIN32
/*!	Runs \a argv detached (no shell, so nothing in it is interpreted, and
	no waiting for the program). Returns whether it could be started.
*/
static bool
run_detached(const char* const argv[])
{
	int pipeFDs[2];
	if (pipe(pipeFDs) != 0)
		return false;

	pid_t child = fork();
	if (child < 0) {
		close(pipeFDs[0]);
		close(pipeFDs[1]);
		return false;
	}

	if (child == 0) {
		close(pipeFDs[0]);
		// Not ours: drop what an AppImage's AppRun set up for the bundle
		const char* appDir = getenv("APPDIR");
		size_t length = appDir != NULL ? strlen(appDir) : 0;
		const char* bundleVariables[] = { "FONTCONFIG_FILE", "TRANSLATORS" };
		for (size_t i = 0; length > 0 && i < 2; i++) {
			const char* value = getenv(bundleVariables[i]);
			if (value != NULL && strncmp(value, appDir, length) == 0)
				unsetenv(bundleVariables[i]);
		}
		setsid();
		pid_t grandchild = fork();
		if (grandchild == 0) {
			close(pipeFDs[1]);
			execvp(argv[0], (char* const*)argv);
			_exit(127);
		}
		char started = grandchild > 0;
		ssize_t written = write(pipeFDs[1], &started, 1);
		(void)written;
		_exit(0);
	}

	close(pipeFDs[1]);
	char started = 0;
	if (read(pipeFDs[0], &started, 1) != 1)
		started = 0;
	close(pipeFDs[0]);
	waitpid(child, NULL, 0);
	return started != 0;
}
#endif


status_t
BNotification::AddAction(const BString& label, BMessage* message)
{
	return AddAction(label, message, be_app_messenger);
}


status_t
BNotification::AddAction(const BString& label, BMessage* message,
	const BMessenger& target)
{
	if (message == NULL)
		return B_BAD_VALUE;

	if (fActions == NULL) {
		fActions = new(std::nothrow) BPrivate::notification_actions;
		if (fActions == NULL) {
			delete message;
			return B_NO_MEMORY;
		}
	}

	BPrivate::notification_action action = { label, message, target };
	fActions->push_back(action);
	return B_OK;
}


int32
BNotification::CountActions() const
{
	return fActions != NULL ? (int32)fActions->size() : 0;
}


const char*
BNotification::ActionLabelAt(int32 index) const
{
	if (index < 0 || index >= CountActions())
		return NULL;
	return (*fActions)[index].label.String();
}


const BMessage*
BNotification::ActionMessageAt(int32 index) const
{
	if (index < 0 || index >= CountActions())
		return NULL;
	return (*fActions)[index].message;
}


BMessenger
BNotification::ActionTargetAt(int32 index) const
{
	if (index < 0 || index >= CountActions())
		return BMessenger();
	return (*fActions)[index].target;
}


status_t
BNotification::Send(bigtime_t timeout)
{
	// Hokku: as on Haiku, to the notification_server when it runs (the
	// Deskbar starts it on Linux), which shows it in its own windows.
	BMessenger server(kNotificationServerSignature);
	if (server.IsValid()) {
		BMessage message(kNotificationMessage);
		status_t status = Archive(&message);
		if (status == B_OK && timeout > 0)
			status = message.AddInt64("timeout", timeout);
		if (status == B_OK)
			status = server.SendMessage(&message);
		if (status == B_OK)
			return B_OK;
	}

	// Otherwise to the desktop's notifications. TODO: icon not supported yet
#ifdef _WIN32
	(void)timeout;
	return B_ERROR; // Not supported on Windows yet
#else
	// Hokku: the title and content often hold text from elsewhere (a chat
	// line, a file name from the network), so they are passed as arguments,
	// never through a shell. This used system("notify-send \"...\"").
	const char* title = fTitle.String() != NULL ? fTitle.String() : "";
	const char* content = fContent.String() != NULL ? fContent.String() : "";
#	ifdef __APPLE__
	(void)timeout;
	const char* argv[] = { "osascript",
		"-e", "on run argv",
		"-e", "display notification (item 2 of argv) with title (item 1 of argv)",
		"-e", "end run",
		title, content, NULL };
#	else
	char milliseconds[32];
	snprintf(milliseconds, sizeof(milliseconds), "%lld",
		(long long)(timeout > 0 ? timeout / 1000 : 0));
	const char* argv[] = { "notify-send", "--", title, content, NULL, NULL,
		NULL };
	if (timeout > 0) {
		// notify-send's -t takes milliseconds; options go before "--"
		argv[1] = "-t";
		argv[2] = milliseconds;
		argv[3] = "--";
		argv[4] = title;
		argv[5] = content;
	}
#	endif
	return run_detached(argv) ? B_OK : B_ERROR;
#endif
}


void BNotification::_ReservedNotification1() {}
void BNotification::_ReservedNotification2() {}
void BNotification::_ReservedNotification3() {}
void BNotification::_ReservedNotification4() {}
void BNotification::_ReservedNotification5() {}
void BNotification::_ReservedNotification6() {}
void BNotification::_ReservedNotification7() {}
void BNotification::_ReservedNotification8() {}
