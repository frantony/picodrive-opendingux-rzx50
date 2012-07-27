// Some mp3 related code for Sega/Mega CD.
// Uses the Helix Fixed-point MP3 decoder

// (c) Copyright 2007, Grazvydas "notaz" Ignotas

#include <stdio.h>
#include <string.h>

#include "../../Pico/PicoInt.h"
#include "../../Pico/sound/mix.h"
#include "lprintf.h"

int mp3_get_bitrate(FILE *f, int len)
{
	return 0;
}


#ifdef __GP2X__

static int mp3_decode(void)
{
	return 0;
}

void mp3_start_local(void)
{
}

static FILE *mp3_current_file = NULL;
static int mp3_file_len = 0, mp3_file_pos = 0;
static unsigned char mp3_input_buffer[2*1024];

void mp3_start_play(FILE *f, int pos)
{
}

int mp3_get_offset(void)
{
	return 0;
}

#endif // ifndef __GP2X__

void mp3_update(int *buffer, int length, int stereo)
{
}


