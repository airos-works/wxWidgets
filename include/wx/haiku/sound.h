/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/sound.h
// Purpose:     wxSound for the Haiku port, on the Game Kit's BFileGameSound
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_SOUND_H_
#define _WX_HAIKU_SOUND_H_

#include "wx/buffer.h"

// Plays any sound file the Media Kit has a decoder for. One sound plays at a
// time, as in the other ports: starting one stops the previous one.
class WXDLLIMPEXP_ADV wxSound : public wxSoundBase
{
public:
    wxSound() { }
    wxSound(const wxString& fileName, bool isResource = false)
        { Create(fileName, isResource); }
    wxSound(size_t size, const void* data)
        { Create(size, data); }
    virtual ~wxSound();

    // Create from a file; resources are not supported
    bool Create(const wxString& fileName, bool isResource = false);

    // Create from data in memory (the contents of a sound file)
    bool Create(size_t size, const void* data);

    bool IsOk() const { return !m_file.empty() || m_data.GetDataLen() != 0; }

    // Stop playing any sound
    static void Stop();

    // Returns true if a sound is being played
    static bool IsPlaying();

protected:
    virtual bool DoPlay(unsigned flags) const wxOVERRIDE;

private:
    wxString m_file;
    wxMemoryBuffer m_data;

    wxDECLARE_NO_COPY_CLASS(wxSound);
};

#endif // _WX_HAIKU_SOUND_H_
