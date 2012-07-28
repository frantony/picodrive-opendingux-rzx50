/* faking/emulating gp2x.c by using gtk */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/keyboard.h> 

#include <ao/ao.h>

#include "../gp2x/emu.h"
#include "../gp2x/gp2x.h"
#include "../gp2x/usbjoy.h"
#include "../gp2x/version.h"

#include "log_io.h"

/* Define this to the CPU frequency */
#define CPU_FREQ 336000000    /* CPU clock: 336 MHz */
#define CFG_EXTAL 12000000    /* EXT clock: 12 Mhz */

// SDRAM Timings, unit: ns
#define SDRAM_TRAS		45	/* RAS# Active Time */
#define SDRAM_RCD		20	/* RAS# to CAS# Delay */
#define SDRAM_TPC		20	/* RAS# Precharge Time */
#define SDRAM_TRWL		7	/* Write Latency Time */
//#define SDRAM_TREF	        15625	/* Refresh period: 4096 refresh cycles/64ms */ 
#define SDRAM_TREF      7812  /* Refresh period: 8192 refresh cycles/64ms */


void *gp2x_screen;
static int current_bpp = 8;
static int current_pal[256];
ao_device *ao_dev = NULL;
int fbfd = 0;
long int screensize = 0;
unsigned short *fbp = 0;    	
int inputfd = 0;

// dummies
char *ext_menu = 0, *ext_state = 0;



/* RIPPED FROM THE SDL LIBS */

int keyboard_fd = -1, saved_kbd_mode = -1, current_vt = -1, saved_vt = -1;
struct termios saved_kbd_termios; 

#define SDL_arraysize(array)	(sizeof(array)/sizeof(array[0])) 

int OpenKeyboard(void)
{
	/* Open only if not already opened */
 	if ( keyboard_fd < 0 ) {
		static const char * const tty0[] = { "/dev/tty0", "/dev/vc/0", NULL };
		static const char * const vcs[] = { "/dev/vc/%d", "/dev/tty%d", NULL };
		int i, tty0_fd;

		/* Try to query for a free virtual terminal */
		tty0_fd = -1;
		for ( i=0; tty0[i] && (tty0_fd < 0); ++i ) {
			tty0_fd = open(tty0[i], O_WRONLY, 0);
		}
		if ( tty0_fd < 0 ) {
			tty0_fd = dup(0); /* Maybe stdin is a VT? */
		}
		ioctl(tty0_fd, VT_OPENQRY, &current_vt);
		close(tty0_fd);

		if ( (geteuid() == 0) && (current_vt > 0) ) {
			for ( i=0; vcs[i] && (keyboard_fd < 0); ++i ) {
				char vtpath[12];

				snprintf(vtpath, SDL_arraysize(vtpath), vcs[i], current_vt);
				keyboard_fd = open(vtpath, O_RDWR, 0);

				/* This needs to be our controlling tty
				   so that the kernel ioctl() calls work
				*/
				if ( keyboard_fd >= 0 ) {
					tty0_fd = open("/dev/tty", O_RDWR, 0);
					if ( tty0_fd >= 0 ) {
						ioctl(tty0_fd, TIOCNOTTY, 0);
						close(tty0_fd);
					}
				}
			}
		}
 		if ( keyboard_fd < 0 ) {
			/* Last resort, maybe our tty is a usable VT */
			struct vt_stat vtstate;

			keyboard_fd = open("/dev/tty", O_RDWR);

			if ( ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0 ) {
				current_vt = vtstate.v_active;
			} else {
				current_vt = 0;
			}
 		}

		/* Make sure that our input is a console terminal */
		{ int dummy;
		  if ( ioctl(keyboard_fd, KDGKBMODE, &dummy) < 0 ) {
			close(keyboard_fd);
			keyboard_fd = -1;
			printf("Unable to open a console terminal");
		  }
		}

 	}
 	return(keyboard_fd);
} 

int InGraphicsMode(void)
{
	return((keyboard_fd >= 0) && (saved_kbd_mode >= 0));
}

void LeaveGraphicsMode(void)
{
	if ( InGraphicsMode() ) {
		ioctl(keyboard_fd, KDSETMODE, KD_TEXT);
		ioctl(keyboard_fd, KDSKBMODE, saved_kbd_mode);
		tcsetattr(keyboard_fd, TCSAFLUSH, &saved_kbd_termios);
		saved_kbd_mode = -1;

		/* Head back over to the original virtual terminal */
		ioctl(keyboard_fd, VT_UNLOCKSWITCH, 1);
		if ( saved_vt > 0 ) {
			ioctl(keyboard_fd, VT_ACTIVATE, saved_vt);
		}
	}
}
 
void CloseKeyboard(void)
{
	if ( keyboard_fd >= 0 ) {
		LeaveGraphicsMode();
		if ( keyboard_fd > 0 ) {
			close(keyboard_fd);
		}
	}
	keyboard_fd = -1;
}
  
int EnterGraphicsMode(void)
{
	struct termios keyboard_termios;

	/* Set medium-raw keyboard mode */
	if ( (keyboard_fd >= 0) && !InGraphicsMode() ) {

		/* Switch to the correct virtual terminal */
		if ( current_vt > 0 ) {
			struct vt_stat vtstate;

			if ( ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0 ) {
				saved_vt = vtstate.v_active;
			}
			if ( ioctl(keyboard_fd, VT_ACTIVATE, current_vt) == 0 ) {
				ioctl(keyboard_fd, VT_WAITACTIVE, current_vt);
			}
		}

		/* Set the terminal input mode */
		if ( tcgetattr(keyboard_fd, &saved_kbd_termios) < 0 ) {
			printf("ERROR: Unable to get terminal attributes\n");
			if ( keyboard_fd > 0 ) {
				close(keyboard_fd);
			}
			keyboard_fd = -1;
			return(-1);
		}
		if ( ioctl(keyboard_fd, KDGKBMODE, &saved_kbd_mode) < 0 ) {
			printf("ERROR: Unable to get current keyboard mode\n");
			if ( keyboard_fd > 0 ) {
				close(keyboard_fd);
			}
			keyboard_fd = -1;
			return(-1);
		}
		keyboard_termios = saved_kbd_termios;
		keyboard_termios.c_lflag &= ~(ICANON | ECHO | ISIG);
		keyboard_termios.c_iflag &= ~(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON);
		keyboard_termios.c_cc[VMIN] = 0;
		keyboard_termios.c_cc[VTIME] = 0;
		if (tcsetattr(keyboard_fd, TCSAFLUSH, &keyboard_termios) < 0) {
			CloseKeyboard();
			printf("ERROR: Unable to set terminal attributes\n");
			return(-1);
		}
		/* This will fail if we aren't root or this isn't our tty */
		if ( ioctl(keyboard_fd, KDSKBMODE, K_MEDIUMRAW) < 0 ) {
			CloseKeyboard();
			printf("ERORR: Unable to set keyboard in raw mode\n");
			return(-1);
		}
		if ( ioctl(keyboard_fd, KDSETMODE, KD_GRAPHICS) < 0 ) {
			CloseKeyboard();
			printf("ERROR: Unable to set keyboard in graphics mode");
			return(-1);
		}
		/* Prevent switching the virtual terminal */
		ioctl(keyboard_fd, VT_LOCKSWITCH, 1);
	}
	return(keyboard_fd);
} 

void gp2x_init(void)
{
	printf("KEYBOARD = %d\n", OpenKeyboard());
	printf("GRAPHICS = %d\n", EnterGraphicsMode());
	printf("entering init()\n"); fflush(stdout);    
	gp2x_screen = malloc(320*240*2 + 320*2);
	memset(gp2x_screen, 0, 320*240*2 + 320*2);

	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;

	// Open the file for reading and writing
	fbfd = open("/dev/fb0", O_RDWR);
	if (!fbfd) {
		printf("Error: cannot open framebuffer device.\n");
		exit(1);
	}
	printf("The framebuffer device was opened successfully.\n");

	// Get fixed screen information
	if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
		printf("Error reading fixed information.\n");
		exit(2);
	}

	// Get variable screen information
	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
		printf("Error reading variable information.\n");
		exit(3);
	}

	printf("%dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel );

	// Figure out the size of the screen in bytes
	screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	// Map the device to memory
	fbp = (unsigned short *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED,
				fbfd, 0);
	if ((int)fbp == -1) {
		printf("Error: failed to map framebuffer device to memory.\n");
		exit(4);
	}
	printf("The framebuffer device was mapped to memory successfully.\n");
	memset(fbp, 0, screensize);

	inputfd = open("/dev/event0", O_RDWR | O_NONBLOCK);

	// snd
	ao_initialize();

	//while(jz_lcdregl[0xa8>>2] & 1);
	//jz_lcdregl[0xa4>>2] = 0;
	
	//printf("PGIOD = %x\n", jz_gpioregl[0x300>>2]);
}

void gp2x_deinit(void)
{
	LeaveGraphicsMode();
	CloseKeyboard();
	free(gp2x_screen);

	if (ao_dev) ao_close(ao_dev);
	ao_dev = NULL;
	ao_shutdown();
	munmap(fbp, screensize);
	close(fbfd);
	close(inputfd);
}

/* video */
void gp2x_video_flip(void)
{
	int i;
	if (current_bpp == 8)
	{
		unsigned char *pixels = gp2x_screen;

		for (i = 320*240; i--;)
		{
			fbp[i] = current_pal[pixels[i]];
		}
	}
	else
	{
		unsigned short *pixels = gp2x_screen;

		for (i = 320*240; i--;)
		{
			fbp[i] = pixels[i];
		}
	}

}

void gp2x_video_flip2(void)
{
	gp2x_video_flip();
}

void gp2x_video_changemode(int bpp)
{
	current_bpp = bpp;
}

void gp2x_video_changemode2(int bpp)
{
	current_bpp = bpp;
}

void gp2x_video_setpalette(int *pal, int len)
{
	memcpy(current_pal, pal, len*4);
}

void gp2x_video_flush_cache(void)
{
}

void gp2x_video_RGB_setscaling(int v_offs, int W, int H)
{
}

void gp2x_memcpy_buffers(int buffers, void *data, int offset, int len)
{
	if ((char *)gp2x_screen + offset != data)
		memcpy((char *)gp2x_screen + offset, data, len);
}

void gp2x_memcpy_all_buffers(void *data, int offset, int len)
{
	memcpy((char *)gp2x_screen + offset, data, len);
}


void gp2x_memset_all_buffers(int offset, int byte, int len)
{
	memset((char *)gp2x_screen + offset, byte, len);
}

void gp2x_pd_clone_buffer2(void)
{
	memset(gp2x_screen, 0, 320*240*2);
}

/* sound */
static int s_oldrate = 0, s_oldbits = 0, s_oldstereo = 0;

void gp2x_sound_volume(int l, int r)
{
}

void gp2x_start_sound(int rate, int bits, int stereo)
{
	int frag = 0, bsize, buffers;

	// if no settings change, we don't need to do anything
	if (rate == s_oldrate && s_oldbits == bits && s_oldstereo == stereo) return;

	// calculate buffer size
	buffers = 12;
	bsize = rate / 32;
	if (rate > 22050) { bsize*=4; buffers*=2; } // 44k mode seems to be very demanding
	while ((bsize>>=1)) frag++;
	frag |= buffers<<16; // 16 buffers

	ao_sample_format ao = { bits, rate, stereo+1, AO_FMT_LITTLE, NULL /*"L,R"*/, };
	ao_dev = ao_open_live(ao_default_driver_id(), &ao, NULL);

	printf("gp2x_set_sound: %i/%ibit/%s, %i buffers of %i bytes\n",
		rate, bits, stereo?"stereo":"mono", frag>>16, 1<<(frag&0xffff));

	s_oldrate = rate; s_oldbits = bits; s_oldstereo = stereo;
}

void gp2x_sound_write(void *buff, int len)
{
	ao_play(ao_dev, buff, len);
}

void gp2x_sound_sync(void)
{
}

/* joy */
#define CASE(key, but) \
  case KEY_##key: \
	button_states &= ~GP2X_##but; \
	if (ev.value) button_states |= GP2X_##but; \
	break

static unsigned long button_states = 0;
unsigned long gp2x_joystick_read(int allow_usb_joy)
{
	struct input_event ev;
	while (read(inputfd, &ev, sizeof(struct input_event)) > 0) {
		switch (ev.code) {
			CASE(UP, UP);
			CASE(DOWN, DOWN);
			CASE(LEFT, LEFT);
			CASE(RIGHT, RIGHT);
			CASE(LEFTCTRL, B);
			CASE(LEFTALT, X);
			CASE(SPACE, Y);
			CASE(LEFTSHIFT, A);
			CASE(TAB, L);
			CASE(BACKSPACE, R);
			CASE(ENTER, START);
			CASE(ESC, SELECT);
			CASE(PAUSE, PUSH);
		}
	}

	return button_states;
}

/* 940 */
int crashed_940 = 0;
void Pause940(int yes)
{
}

void Reset940(int yes, int bank)
{
}

/* faking gp2x cpuctrl.c */
void cpuctrl_init(void)
{
}

void cpuctrl_deinit(void)
{
}

void set_FCLK(unsigned MHZ)
{
}

void Disable_940(void)
{
}

void gp2x_video_wait_vsync(void)
{
}

void set_RAM_Timings(int tRC, int tRAS, int tWR, int tMRD, int tRFC, int tRP, int tRCD)
{
}

void set_gamma(int g100, int A_SNs_curve)
{
}

void set_LCD_custom_rate(int rate)
{
}

void unset_LCD_custom_rate(void)
{
}

/* squidgehack.c */
int mmuhack(void)
{
	return 0;
}


int mmuunhack(void)
{
	return 0;
}


/* misc */
void spend_cycles(int c)
{
	usleep(c/200);
}

/* lprintf */
void lprintf(const char *fmt, ...)
{
#ifndef A320
	va_list vl;

	va_start(vl, fmt);
	vprintf(fmt, vl);
	va_end(vl);
#endif	
}

