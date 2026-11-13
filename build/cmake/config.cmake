#############################################################################
# Name:        build/cmake/config.cmake
# Purpose:     Build wx-config script in build folder
# Author:      Tobias Taschner
# Created:     2016-10-13
# Copyright:   (c) 2016 wxWidgets development team
# Licence:     wxWindows licence
#############################################################################

set(wxCONFIG_DIR ${wxOUTPUT_DIR}/wx/config)
file(MAKE_DIRECTORY ${wxCONFIG_DIR})
set(TOOLCHAIN_FULLNAME ${wxBUILD_FILE_ID})

macro(wx_configure_script input output)
    set(abs_top_srcdir ${CMAKE_CURRENT_SOURCE_DIR})
    set(abs_top_builddir ${CMAKE_CURRENT_BINARY_DIR})

    configure_file(
        ${input}
        ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${output}
        ESCAPE_QUOTES @ONLY NEWLINE_STYLE UNIX)
    file(COPY
        ${CMAKE_CURRENT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/${output}
        DESTINATION ${wxCONFIG_DIR}
        FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
            GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
        )
endmacro()

# wx_get_dependencies(<var> <lib>)
# The linker arguments an application linking the (static) wx library needs
# in addition to the wx libraries themselves, which wx-config adds, in the
# right order, on its own.
macro(wx_get_dependencies var lib)
    set(${var})
    if(TARGET wx${lib})
        get_target_property(deps wx${lib} LINK_LIBRARIES)
        foreach(dep IN LISTS deps)
            set(dep_name)
            if(TARGET ${dep})
                list(FIND wxLIB_TARGETS ${dep} dep_is_wxlib)
                if(dep_is_wxlib EQUAL -1)
                    # a library of ours which is not a wx library proper
                    # (the builtin regex, png...)
                    get_target_property(dep_type ${dep} TYPE)
                    if (dep_type STREQUAL "INTERFACE_LIBRARY")
                        get_target_property(dep_name ${dep} INTERFACE_OUTPUT_NAME)
                        set(dep_name "-l${dep_name}")
                    else()
                        # These are static libraries installed with the wx
                        # ones: with their path like those (wx-config
                        # defines libdir), CMake's FindwxWidgets looks for
                        # every -l library and does not find them when cross
                        # compiling, the wx prefix being no system root.
                        get_target_property(dep_name ${dep} OUTPUT_NAME)
                        set(dep_name "\${libdir}/lib${dep_name}.a")
                    endif()
                endif()
            elseif(dep MATCHES "^-")
                # a linker option
                set(dep_name "${dep}")
            elseif(IS_ABSOLUTE "${dep}")
                # A library found by find_package(). By name if the compiler
                # looks in its directory by itself (it is in the sysroot),
                # with its path otherwise.
                get_filename_component(dep_file "${dep}" NAME)
                if(CMAKE_SYSROOT AND dep MATCHES "^${CMAKE_SYSROOT}/"
                        AND dep_file MATCHES "^lib(.+)\\.(so|a)$")
                    set(dep_name "-l${CMAKE_MATCH_1}")
                else()
                    set(dep_name "${dep}")
                endif()
            else()
                # a library given by name
                set(dep_name "-l${dep}")
            endif()
            if(dep_name)
                wx_string_append(${var} "${dep_name} ")
            endif()
        endforeach()
        string(STRIP "${${var}}" ${var})
    endif()
endmacro()

function(wx_write_config_inplace)
    wx_configure_script(
        "${CMAKE_CURRENT_SOURCE_DIR}/wx-config-inplace.in"
        "inplace-${TOOLCHAIN_FULLNAME}"
        )
    execute_process(
        COMMAND
        ${CMAKE_COMMAND} -E create_symlink
        "lib/wx/config/inplace-${TOOLCHAIN_FULLNAME}"
        "${CMAKE_CURRENT_BINARY_DIR}/wx-config"
        )
endfunction()

function(wx_write_config)

    set(prefix ${CMAKE_INSTALL_PREFIX})
    set(exec_prefix "\${prefix}")
    set(includedir "\${prefix}/include")
    set(libdir "\${exec_prefix}/lib")
    set(bindir "\${exec_prefix}/bin")

    find_program(EGREP egrep)
    mark_as_advanced(EGREP)

    if(wxBUILD_MONOLITHIC)
        set(MONOLITHIC 1)
    else()
        set(MONOLITHIC 0)
    endif()
    if(wxBUILD_SHARED)
        set(SHARED 1)
    else()
        set(SHARED 0)
    endif()
    if(wxUSE_UNICODE)
        set(WX_CHARTYPE unicode)
        set(lib_unicode_suffix u)
    else()
        set(WX_CHARTYPE ansi)
        set(lib_unicode_suffix)
    endif()
    if(CMAKE_CROSSCOMPILING AND NOT HAIKU)
        set(cross_compiling yes)
        set(host_alias ${CMAKE_SYSTEM_NAME})
    else()
        # (Haiku is always built from elsewhere, for one system: its
        # libraries have no -<host> suffix and its wx-config does not select
        # a host, see wx_set_target_properties())
        set(cross_compiling no)
    endif()

    set(BUILT_WX_LIBS)
    set(STD_BASE_LIBS)
    set(STD_GUI_LIBS)
    set(STD_BASE_LIBS_ALL xml net base)
    set(STD_GUI_LIBS_ALL xrc html qa adv core)
    foreach(lib IN ITEMS xrc webview stc richtext ribbon propgrid aui gl media html qa adv core xml net base)
        list(FIND wxLIB_TARGETS wx${lib} hasLib)
        if (hasLib GREATER -1)
            wx_string_append(BUILT_WX_LIBS "${lib} ")
            list(FIND STD_BASE_LIBS_ALL ${lib} index)
            if (index GREATER -1)
                wx_string_append(STD_BASE_LIBS "${lib} ")
            endif()
            list(FIND STD_GUI_LIBS_ALL ${lib} index)
            if (index GREATER -1)
                wx_string_append(STD_GUI_LIBS "${lib} ")
            endif()
        endif()
    endforeach()
    string(STRIP "${BUILT_WX_LIBS}" BUILT_WX_LIBS)
    string(STRIP "${STD_BASE_LIBS}" STD_BASE_LIBS)
    string(STRIP "${STD_GUI_LIBS}" STD_GUI_LIBS)

    set(WX_RELEASE ${wxMAJOR_VERSION}.${wxMINOR_VERSION})
    set(WX_VERSION ${wxVERSION})
    set(WX_SUBVERSION ${wxVERSION}.0)
    set(WX_FLAVOUR)
    set(TOOLKIT_DIR ${wxBUILD_TOOLKIT})
    set(TOOLKIT_VERSION)
    set(WIDGET_SET ${wxBUILD_WIDGETSET})
    set(TOOLCHAIN_NAME "${TOOLKIT_DIR}${TOOLKIT_VERSION}${WIDGET_SET}${lib_unicode_suffix}-${WX_RELEASE}")
    set(WX_LIBRARY_BASENAME_GUI "wx_${TOOLKIT_DIR}${TOOLKIT_VERSION}${WIDGET_SET}${lib_unicode_suffix}")
    set(WX_LIBRARY_BASENAME_NOGUI "wx_base${lib_unicode_suffix}")

    wx_get_dependencies(WXCONFIG_LIBS base)
    wx_get_dependencies(EXTRALIBS_GUI core)
    set(EXTRALIBS_SDL) # included in core libs when SDL is enabled
    wx_get_dependencies(EXTRALIBS_HTML html)
    wx_get_dependencies(EXTRALIBS_STC stc)
    wx_get_dependencies(EXTRALIBS_WEBVIEW webview)
    wx_get_dependencies(EXTRALIBS_XML xml)
    wx_get_dependencies(EXTRALIBS_MEDIA media)
    wx_get_dependencies(EXTRALIBS_NET net)
    wx_get_dependencies(OPENGL_LIBS gl)
    set(DMALLOC_LIBS)
    if(wxBUILD_MONOLITHIC)
        wx_get_dependencies(WXCONFIG_LIBS mono)
    endif()

    set(CC ${CMAKE_C_COMPILER})
    set(CXX ${CMAKE_CXX_COMPILER})
    set(WXCONFIG_CFLAGS)
    set(WXCONFIG_LDFLAGS)
    if(CMAKE_USE_PTHREADS_INIT AND NOT HAIKU)
        # (threads are part of Haiku's libroot)
        set(WXCONFIG_CFLAGS "-pthread")
        set(WXCONFIG_LDFLAGS "-pthread")
    endif()
    set(WXCONFIG_CPPFLAGS)
    if(wxBUILD_SHARED)
        set(WXCONFIG_CPPFLAGS "-DWXUSINGDLL")
    endif()
    foreach(flag IN LISTS wxTOOLKIT_DEFINITIONS)
        wx_string_append(WXCONFIG_CPPFLAGS " -D${flag}")
    endforeach()
    if(NOT wxBUILD_DEBUG_LEVEL STREQUAL "Default")
        # the library was compiled with this level (see init.cmake) and
        # applications must be too: with another one they call assertion
        # functions the library does not have or lacks those it expects
        wx_string_append(WXCONFIG_CPPFLAGS " -DwxDEBUG_LEVEL=${wxBUILD_DEBUG_LEVEL}")
    endif()
    string(STRIP "${WXCONFIG_CPPFLAGS}" WXCONFIG_CPPFLAGS)
    set(WXCONFIG_CXXFLAGS ${WXCONFIG_CFLAGS})
    set(WXCONFIG_LDFLAGS_GUI)
    set(WXCONFIG_RESFLAGS)
    set(WXCONFIG_RPATH)
    if(wxBUILD_SHARED)
        set(WXCONFIG_RPATH "-Wl,-rpath,\$libdir")
    endif()
    set(LDFLAGS_GL)
    set(RESCOMP)

    wx_configure_script(
        "${CMAKE_CURRENT_SOURCE_DIR}/wx-config.in"
        "${TOOLCHAIN_FULLNAME}"
        )
endfunction()

wx_write_config_inplace()
wx_write_config()
