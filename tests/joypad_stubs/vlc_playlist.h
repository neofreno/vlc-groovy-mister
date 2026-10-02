#pragma once
#include "vlc_common.h"
struct playlist_t : vlc_object_t {};
struct input_thread_t : vlc_object_t {};
input_thread_t* playlist_CurrentInput(playlist_t*);
input_thread_t* playlist_CurrentInputLocked(playlist_t*);
void playlist_Lock(playlist_t*);
void playlist_Unlock(playlist_t*);
constexpr int PLAYLIST_TOGGLE_PAUSE = 1, PLAYLIST_STOP = 2, PLAYLIST_SKIP = 3;
constexpr bool pl_Locked = true;
void playlist_Control(playlist_t*, int, bool, ...);
void playlist_TogglePause(playlist_t*);
void playlist_Stop(playlist_t*);
void playlist_Prev(playlist_t*);
void playlist_Next(playlist_t*);
