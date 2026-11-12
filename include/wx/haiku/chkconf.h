/*
 * Name:        wx/haiku/chkconf.h
 * Purpose:     wxHaiku-specific configuration checks
 * Copyright:   (c) 2026 air/OS project
 * Licence:     wxWindows licence
 */

/* THIS IS A C FILE, DON'T USE C++ FEATURES (IN PARTICULAR COMMENTS) IN IT */

#ifndef _WX_HAIKU_CHKCONF_H_
#define _WX_HAIKU_CHKCONF_H_

#ifndef __WXUNIVERSAL__
#   error "wxHaiku is a wxUniversal port: __WXUNIVERSAL__ must be defined"
#endif

#if !wxUSE_CONSOLE_EVENTLOOP
#   ifdef wxABORT_ON_CONFIG_ERROR
#       error "wxHaiku requires wxUSE_CONSOLE_EVENTLOOP"
#   else
#       undef wxUSE_CONSOLE_EVENTLOOP
#       define wxUSE_CONSOLE_EVENTLOOP 1
#   endif
#endif

#if !wxUSE_UNICODE
#   error "wxHaiku requires wxUSE_UNICODE"
#endif

/* Not available on this port (the Be API has no equivalent or it is unused) */
#undef wxUSE_PALETTE
#define wxUSE_PALETTE 0

#undef wxUSE_METAFILE
#define wxUSE_METAFILE 0

#undef wxUSE_ENH_METAFILE
#define wxUSE_ENH_METAFILE 0

#undef wxUSE_JOYSTICK
#define wxUSE_JOYSTICK 0

#undef wxUSE_DIALUP_MANAGER
#define wxUSE_DIALUP_MANAGER 0

#undef wxUSE_DETECT_SM
#define wxUSE_DETECT_SM 0

#endif /* _WX_HAIKU_CHKCONF_H_ */
