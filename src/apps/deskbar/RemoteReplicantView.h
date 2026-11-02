/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef REMOTE_REPLICANT_VIEW_H
#define REMOTE_REPLICANT_VIEW_H


#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <String.h>
#include <View.h>


class BBitmap;
class BMenu;
class BMessageRunner;


/*!	Stands in for a replicant whose code Deskbar can't load (an app's, on
	Linux): the app runs the replicant and this view shows what it draws and
	passes the pointer back. See RemoteReplicant.h.
*/
class _EXPORT RemoteReplicantView : public BView {
public:
								RemoteReplicantView(BMessage* archive);
	virtual						~RemoteReplicantView();

	// Wraps an archive Deskbar can't instantiate into one for this class.
	static	status_t			Wrap(const BMessage* replicantArchive,
									BMessage* into);

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* into,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameMoved(BPoint newPosition);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseUp(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Start();
			void				_Started(BMessage* message);
			void				_Disconnect();
			void				_CopyPixels();
			void				_Forward(BPoint where);
			void				_ShowMenu(BMessage* message);
	static	void				_AddMenuItems(BMenu* menu, BMessage* archive,
									BMessage* path, BHandler* target);

			BMessage			fReplicantArchive;
			BString				fSignature;
			BMessenger			fReplicant;
			BMessageRunner*		fCheckRunner;
			area_id				fArea;
			const uint8*		fAreaBase;
			BBitmap*			fBitmap;
			bool				fStarting;
			bigtime_t			fStartTime;
			int32				fButtons;
};


#endif	// REMOTE_REPLICANT_VIEW_H
