/* faking/emulating gp2x.c by using gtk */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#include <linux/fb.h>
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

#include "../gp2x/emu.h"
#include "../gp2x/gp2x.h"
#include "../gp2x/usbjoy.h"
#include "../gp2x/version.h"

#include "log_io.h"

#include "jz4740.h"

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
static int sounddev = 0, mixerdev = 0;
int fbfd = 0;
long int screensize = 0;
unsigned short *fbp = 0;    	

static unsigned long jz_dev=0;
static volatile unsigned long  *jz_cdcregl, *jz_cpmregl, *jz_emcregl, *jz_gpioregl, *jz_lcdregl;
volatile unsigned short *jz_emcregs;


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
	
	// snd
  	mixerdev = open("/dev/mixer", O_RDWR);
	if (mixerdev == -1)
		printf("open(\"/dev/mixer\") failed with %i\n", errno);

	printf("exitting init()\n"); fflush(stdout);
	
	if(!jz_dev)  jz_dev = open("/dev/mem", O_RDWR);  
	jz_gpioregl=(unsigned long  *)mmap(0, 0x300, PROT_READ|PROT_WRITE, MAP_SHARED, jz_dev, 0x10010000);
	jz_cpmregl=(unsigned long  *)mmap(0, 0x10, PROT_READ|PROT_WRITE, MAP_SHARED, jz_dev, 0x10000000);
	jz_emcregl=(unsigned long  *)mmap(0, 0x90, PROT_READ|PROT_WRITE, MAP_SHARED, jz_dev, 0x13010000);
	jz_lcdregl=(unsigned long  *)mmap(0, 0x90, PROT_READ|PROT_WRITE, MAP_SHARED, jz_dev, 0x13050000);
	jz_emcregs=(unsigned short *)jz_emcregl;
	
	//while(jz_lcdregl[0xa8>>2] & 1);
	//jz_lcdregl[0xa4>>2] = 0;
	
	//printf("PGIOD = %x\n", jz_gpioregl[0x300>>2]);
}

void gp2x_deinit(void)
{
	LeaveGraphicsMode();
	CloseKeyboard();
	free(gp2x_screen);
	if (sounddev > 0) close(sounddev);
	close(mixerdev);
    	munmap(fbp, screensize);
    	close(fbfd);
	munmap((void *)jz_gpioregl, 0x300);
	munmap((void *)jz_cpmregl, 0x10);
	munmap((void *)jz_emcregl, 0x90);
	close(jz_dev);
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
 	l=l<0?0:l; l=l>255?255:l; r=r<0?0:r; r=r>255?255:r;
 	l<<=8; l|=r;
  	ioctl(mixerdev, SOUND_MIXER_WRITE_VOLUME, &l);
}

void gp2x_start_sound(int rate, int bits, int stereo)
{
	int frag = 0, bsize, buffers;

	// if no settings change, we don't need to do anything
	if (rate == s_oldrate && s_oldbits == bits && s_oldstereo == stereo) return;

	if (sounddev > 0) close(sounddev);
	sounddev = open("/dev/dsp", O_WRONLY|O_ASYNC);
	if (sounddev == -1)
		printf("open(\"/dev/dsp\") failed with %i\n", errno);

	ioctl(sounddev, SNDCTL_DSP_SPEED,  &rate);
	ioctl(sounddev, SNDCTL_DSP_SETFMT, &bits);
	ioctl(sounddev, SNDCTL_DSP_STEREO, &stereo);
	// calculate buffer size
	buffers = 12;
	bsize = rate / 32;
	if (rate > 22050) { bsize*=4; buffers*=2; } // 44k mode seems to be very demanding
	while ((bsize>>=1)) frag++;
	frag |= buffers<<16; // 16 buffers
	ioctl(sounddev, SNDCTL_DSP_SETFRAGMENT, &frag);
	printf("gp2x_set_sound: %i/%ibit/%s, %i buffers of %i bytes\n",
		rate, bits, stereo?"stereo":"mono", frag>>16, 1<<(frag&0xffff));

	s_oldrate = rate; s_oldbits = bits; s_oldstereo = stereo;
	//gp2x_sound_volume(10, 10);
}

void gp2x_sound_write(void *buff, int len)
{
	write(sounddev, buff, len);
}

void gp2x_sound_sync(void)
{
	ioctl(sounddev, SOUND_PCM_SYNC, 0);
}

/* joy */
unsigned long gp2x_joystick_read(int allow_usb_joy)
{
  	unsigned long value;
	value = ~((jz_gpioregl[0x300>>2] & 0x280EC067) | ((jz_gpioregl[0x200>>2] & 0x20000) >> 1) | 0x300000);
	return value;
}

/* 940 */
int crashed_940 = 0;
void Pause940(int yes)
{
}

void Reset940(int yes, int bank)
{
}

inline int sdram_convert(unsigned int pllin,unsigned int *sdram_freq)
{
	register unsigned int ns, dmcr,tmp;
 
	ns = 1000000000 / pllin;
	tmp = SDRAM_TRAS/ns;
	if (tmp < 4) tmp = 4;
	if (tmp > 11) tmp = 11;
	dmcr |= ((tmp-4) << EMC_DMCR_TRAS_BIT);

	tmp = SDRAM_RCD/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_RCD_BIT);

	tmp = SDRAM_TPC/ns;
	if (tmp > 7) tmp = 7;
	dmcr |= (tmp << EMC_DMCR_TPC_BIT);

	tmp = SDRAM_TRWL/ns;
	if (tmp > 3) tmp = 3;
	dmcr |= (tmp << EMC_DMCR_TRWL_BIT);

	tmp = (SDRAM_TRAS + SDRAM_TPC)/ns;
	if (tmp > 14) tmp = 14;
	dmcr |= (((tmp + 1) >> 1) << EMC_DMCR_TRC_BIT);

	/* Set refresh registers */
	tmp = SDRAM_TREF/ns;
	tmp = tmp/64 + 1;
	if (tmp > 0xff) tmp = 0xff;
        *sdram_freq = tmp; 

	return 0;

}
 
void pll_init(unsigned int clock)
{
	register unsigned int cfcr, plcr1;
	unsigned int sdramclock = 0;
	int n2FR[33] = {
		0, 0, 1, 2, 3, 0, 4, 0, 5, 0, 0, 0, 6, 0, 0, 0,
		7, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0,
		9
	};
	//int div[5] = {1, 4, 4, 4, 4}; /* divisors of I:S:P:L:M */
  	int div[5] = {1, 3, 3, 3, 3}; /* divisors of I:S:P:L:M */
	int nf, pllout2;

	cfcr = CPM_CPCCR_CLKOEN |
		(n2FR[div[0]] << CPM_CPCCR_CDIV_BIT) | 
		(n2FR[div[1]] << CPM_CPCCR_HDIV_BIT) | 
		(n2FR[div[2]] << CPM_CPCCR_PDIV_BIT) |
		(n2FR[div[3]] << CPM_CPCCR_MDIV_BIT) |
		(n2FR[div[4]] << CPM_CPCCR_LDIV_BIT);

	pllout2 = (cfcr & CPM_CPCCR_PCS) ? clock : (clock / 2);

	/* Init UHC clock */
//	REG_CPM_UHCCDR = pllout2 / 48000000 - 1;
    	jz_cpmregl[0x6C>>2] = pllout2 / 48000000 - 1;

	nf = clock * 2 / CFG_EXTAL;
	plcr1 = ((nf - 2) << CPM_CPPCR_PLLM_BIT) | /* FD */
		(0 << CPM_CPPCR_PLLN_BIT) |	/* RD=0, NR=2 */
		(0 << CPM_CPPCR_PLLOD_BIT) |    /* OD=0, NO=1 */
		(0x20 << CPM_CPPCR_PLLST_BIT) | /* PLL stable time */
		CPM_CPPCR_PLLEN;                /* enable PLL */          

	/* init PLL */
//	REG_CPM_CPCCR = cfcr;
//	REG_CPM_CPPCR = plcr1;
      	jz_cpmregl[0] = cfcr;
    	jz_cpmregl[0x10>>2] = plcr1;
	
  	sdram_convert(clock,&sdramclock);
  	if(sdramclock > 0)
  	{
//	REG_EMC_RTCOR = sdramclock;
//	REG_EMC_RTCNT = sdramclock;	  
      	jz_emcregs[0x8C>>1] = sdramclock;
    	jz_emcregs[0x88>>1] = sdramclock;	

  	}else
  	{
  	printf("sdram init fail!\n");
  	while(1);
  	} 
	
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
	pll_init(MHZ*1000000);
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

