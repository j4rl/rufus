/* Rufus WinUI 3 host. Keep this boundary usable by the existing C engine. */
#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef RUFUS_WINUI
BOOL WinUIInitialize(HWND dialog);
LPCWSTR WinUIErrorMessage(void);
void WinUIDestroy(void);
void WinUIShutdown(void);
BOOL WinUIIsActive(void);
BOOL WinUIPreTranslateMessage(MSG* message);
void WinUISetProgressState(int state);
void WinUISetProgressMarquee(BOOL enabled);
#else
static __inline BOOL WinUIInitialize(HWND dialog) { (void)dialog; return TRUE; }
static __inline LPCWSTR WinUIErrorMessage(void) { return L""; }
static __inline void WinUIDestroy(void) { }
static __inline void WinUIShutdown(void) { }
static __inline BOOL WinUIIsActive(void) { return FALSE; }
static __inline BOOL WinUIPreTranslateMessage(MSG* message) { (void)message; return FALSE; }
static __inline void WinUISetProgressState(int state) { (void)state; }
static __inline void WinUISetProgressMarquee(BOOL enabled) { (void)enabled; }
#endif

#ifdef __cplusplus
}
#endif
