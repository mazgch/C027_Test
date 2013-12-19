// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently,
// but are changed infrequently

#pragma once

#ifdef _CONSOLE
 
#include "targetver.h"

#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include <windows.h>

#else

#ifndef _SECURE_ATL
#define _SECURE_ATL 1
#endif

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Exclude rarely-used stuff from Windows headers
#endif

#include "targetver.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <tchar.h>
#include <time.h>

#endif 

