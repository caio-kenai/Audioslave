#pragma once
// Windows include preamble. Every translation unit that talks to Win32 / COM
// includes this header FIRST, so <windows.h> is configured consistently and
// juce::ComSmartPtr is available (juce_core only declares it when
// JUCE_CORE_INCLUDE_COM_SMART_PTR is set before juce_core.h is included).

#if defined (JUCE_CORE_H_INCLUDED) && ! defined (JUCE_CORE_INCLUDE_COM_SMART_PTR)
 #error "Include platform/windows/WinCommon.h before any JUCE header in this translation unit"
#endif

#include <windows.h>
#include <objbase.h>
#include <unknwn.h>

#ifndef JUCE_CORE_INCLUDE_COM_SMART_PTR
 #define JUCE_CORE_INCLUDE_COM_SMART_PTR 1
#endif

#include <juce_core/juce_core.h>
