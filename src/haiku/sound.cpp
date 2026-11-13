/////////////////////////////////////////////////////////////////////////////
// Name:        src/haiku/sound.cpp
// Purpose:     wxSound for the Haiku port, on the Game Kit's BFileGameSound
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// for compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SOUND

#include "wx/sound.h"

#ifndef WX_PRECOMP
    #include "wx/log.h"
    #include "wx/module.h"
#endif

#include "wx/filename.h"

#include <DataIO.h>
#include <FileGameSound.h>
#include <OS.h>

#include <new>

// ----------------------------------------------------------------------------
// the sound being played
// ----------------------------------------------------------------------------

namespace
{

// The Game Kit mixes and plays in threads of its own; the object only has to
// stay alive while its sound plays. There is one at a time.
BFileGameSound *gs_playing = NULL;

void StopPlaying()
{
    if ( gs_playing )
    {
        gs_playing->StopPlaying();
        delete gs_playing;
        gs_playing = NULL;
    }
}

class wxSoundCleanupModule : public wxModule
{
public:
    virtual bool OnInit() wxOVERRIDE { return true; }
    virtual void OnExit() wxOVERRIDE { StopPlaying(); }

private:
    wxDECLARE_DYNAMIC_CLASS(wxSoundCleanupModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxSoundCleanupModule, wxModule);

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxSound
// ----------------------------------------------------------------------------

wxSound::~wxSound()
{
}

bool wxSound::Create(const wxString& fileName, bool isResource)
{
    m_file.clear();
    m_data.Clear();

    if ( isResource )
    {
        // there are no sound resources in this port
        return false;
    }

    if ( !wxFileName::FileExists(fileName) )
        return false;

    m_file = fileName;
    return true;
}

bool wxSound::Create(size_t size, const void* data)
{
    m_file.clear();
    m_data.Clear();

    if ( !size || !data )
        return false;

    m_data.AppendData(data, size);
    return true;
}

/* static */
void wxSound::Stop()
{
    StopPlaying();
}

/* static */
bool wxSound::IsPlaying()
{
    return gs_playing && gs_playing->IsPlaying();
}

bool wxSound::DoPlay(unsigned flags) const
{
    if ( !IsOk() )
        return false;

    StopPlaying();

    const bool loop = (flags & wxSOUND_LOOP) != 0;

    BFileGameSound *sound;
    if ( !m_file.empty() )
    {
        sound = new(std::nothrow) BFileGameSound(m_file.utf8_str(), loop);
    }
    else
    {
        // the sound object owns the stream, which has its own copy of the
        // data: the wxSound may be destroyed while its sound plays
        BMallocIO * const io = new BMallocIO;
        io->Write(m_data.GetData(), m_data.GetDataLen());
        io->Seek(0, SEEK_SET);

        sound = new(std::nothrow) BFileGameSound(io, loop);
    }

    // fails without a media server or if no decoder knows the format
    if ( !sound || sound->InitCheck() != B_OK
            || sound->StartPlaying() != B_OK )
    {
        wxLogTrace("sound", "could not play the sound");
        delete sound;
        return false;
    }

    gs_playing = sound;

    if ( !(flags & wxSOUND_ASYNC) )
    {
        // synchronous: this blocks the calling (wx) thread by definition
        while ( gs_playing->IsPlaying() )
            snooze(20000);

        StopPlaying();
    }

    return true;
}

#endif // wxUSE_SOUND
