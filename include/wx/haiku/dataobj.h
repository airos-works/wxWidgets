/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/dataobj.h
// Purpose:     wxDataObject for the native Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_DATAOBJ_H_
#define _WX_HAIKU_DATAOBJ_H_

class BMessage;

class WXDLLIMPEXP_CORE wxDataObject : public wxDataObjectBase
{
public:
    wxDataObject();
    virtual ~wxDataObject();

    virtual bool IsSupportedFormat(const wxDataFormat& format,
                                   Direction dir = Get) const;

    // Adds the data in all the formats of the object to the message (of the
    // clipboard or of a drag); returns false if there was nothing to add.
    bool HaikuAddToMessage(BMessage *message) const;

    // Sets the data of the object from the first of its formats found in
    // the message; returns false if there is none.
    bool HaikuSetFromMessage(const BMessage *message);

    // true if the message has data in the format
    static bool HaikuMessageHasFormat(const BMessage *message,
                                      const wxDataFormat& format);
};

#endif // _WX_HAIKU_DATAOBJ_H_
