/////////////////////////////////////////////////////////////////////////////
// Name:        wx/haiku/private/webview.h
// Purpose:     the seam between wxWebView's common code and the web view
//              backend of the Haiku port
// Copyright:   (c) 2026 air/OS project
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_HAIKU_PRIVATE_WEBVIEW_H_
#define _WX_HAIKU_PRIVATE_WEBVIEW_H_

#include "wx/webview.h"

#if wxUSE_WEBVIEW

// Returns a new factory of the port's default web view backend or NULL if the
// library was built without one. Called (once per lookup while it returns
// NULL) by wxWebView::InitFactoryMap(), which registers the factory as
// wxWebViewBackendWebKit: that is wxWebViewBackendDefault on Haiku.
//
// Defined in src/haiku/webview_haiku.cpp, see there.
wxWebViewFactory *wxHaikuCreateDefaultWebViewFactory();

#endif // wxUSE_WEBVIEW

#endif // _WX_HAIKU_PRIVATE_WEBVIEW_H_
