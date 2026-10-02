#pragma once
#include "vlc_playlist.h"
bool var_GetBool(input_thread_t*, const char*);
int var_CountChoices(input_thread_t*, const char*);
int64_t var_GetInteger(input_thread_t*, const char*);
int var_SetInteger(input_thread_t*, const char*, int64_t);
