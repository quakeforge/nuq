/*
	vid_3dfxsvga.c

	OpenGL device driver for 3Dfx chipsets running Linux

	Copyright (C) 1996-1997  Id Software, Inc.
	Copyright (C) 1999,2000  Nelson Rush.
	Copyright (C) 1999,2000  contributors of the QuakeForge project
	Please see the file "AUTHORS" for a list of contributors

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

	$Id$
*/

#include "qtypes.h"
#include "glquake.h"
#include "sys.h"
#include "console.h"
#include "cvar.h"
#include "sbar.h"
#include "qendian.h"
#include "qargs.h"
//#include "lib_replace.h"
#include "host.h"
#include "quakefs.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif
#ifndef RTLD_LAZY
# ifdef DL_LAZY
#  define RTLD_LAZY	DL_LAZY
# else
#  define RTLD_LAZY	0
# endif
#endif

#include <GL/gl.h>
#include <GL/fxmesa.h>
#include <glide/sst1vid.h>

#define WARP_WIDTH              320
#define WARP_HEIGHT             200


//unsigned short	d_8to16table[256];
unsigned		d_8to24table[256];
unsigned char	d_15to8table[65536];

static cvar_t	*vid_mode;
static cvar_t	*vid_redrawfull;
static cvar_t	*vid_waitforrefresh;
extern cvar_t	*gl_triplebuffer;

#ifdef HAVE_DLOPEN
static void	*dlhand = NULL;
#endif

static fxMesaContext fc = NULL;
static int	scr_width, scr_height;
static qboolean is8bit = 0;

int	VID_options_items = 0;

/*-----------------------------------------------------------------------*/

//int	texture_mode = GL_NEAREST;
//int	texture_mode = GL_NEAREST_MIPMAP_NEAREST;
//int	texture_mode = GL_NEAREST_MIPMAP_LINEAR;
int	texture_mode = GL_LINEAR;
//int	texture_mode = GL_LINEAR_MIPMAP_NEAREST;
//int	texture_mode = GL_LINEAR_MIPMAP_LINEAR;

int	texture_extension_number = 1;

float		gldepthmin, gldepthmax;

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions;

// ARB Multitexture
int gl_mtex_enum = TEXTURE0_SGIS;
qboolean gl_arb_mtex = false;
qboolean gl_mtexable = false;

/*-----------------------------------------------------------------------*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}

void D_EndDirectRect (int x, int y, int width, int height)
{
}

void VID_Shutdown(void)
{
	if (!fc)
		return;

	fxMesaDestroyContext(fc);
}

void signal_handler(int sig)
{
	printf("Received signal %d, exiting...\n", sig);
	Host_Shutdown();
	abort();
	//Sys_Quit();
	exit(0);
}

void InitSig(void)
{
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL, signal_handler);
	signal(SIGTRAP, signal_handler);
//	signal(SIGIOT, signal_handler);
	signal(SIGBUS, signal_handler);
//	signal(SIGFPE, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
}

void VID_ShiftPalette(unsigned char *p)
{
//	VID_SetPalette(p);
}

void	VID_SetPalette (unsigned char *palette)
{
	byte	*pal;
	unsigned r,g,b;
	unsigned v;
	int     r1,g1,b1;
	int		k;
	unsigned short i;
	unsigned	*table;
	FILE *f;
	char s[255];
//#endif
	float dist, bestdist;
//
// 8 8 8 encoding
//
	Con_Printf("Converting 8to24\n");

	pal = palette;
	table = d_8to24table;
	for (i=0 ; i<256 ; i++)
	{
		r = pal[0];
		g = pal[1];
		b = pal[2];
		pal += 3;

//		v = (255<<24) + (r<<16) + (g<<8) + (b<<0);
//		v = (255<<0) + (r<<8) + (g<<16) + (b<<24);
		v = (255<<24) + (r<<0) + (g<<8) + (b<<16);
		*table++ = v;
	}
	d_8to24table[255] = 0;	// 255 is transparent

	// JACK: 3D distance calcs - k is last closest, l is the distance.
	{
		static qboolean palflag = false;

		// FIXME: Precalculate this and cache to disk.
		if (palflag)
			return;
		palflag = true;
	}

	COM_FOpenFile("glquake/15to8.pal", &f);
	if (f) {
		fread(d_15to8table, 1<<15, 1, f);
		fclose(f);
	} else
	{
		for (i=0; i < (1<<15); i++) {
			/* Maps
 			000000000000000
 			000000000011111 = Red  = 0x1F
 			000001111100000 = Blue = 0x03E0
 			111110000000000 = Grn  = 0x7C00
 			*/
 			r = ((i & 0x1F) << 3)+4;
 			g = ((i & 0x03E0) >> 2)+4;
 			b = ((i & 0x7C00) >> 7)+4;
			pal = (unsigned char *)d_8to24table;
			for (v=0,k=0,bestdist=10000.0; v<256; v++,pal+=4) {
 				r1 = (int)r - (int)pal[0];
 				g1 = (int)g - (int)pal[1];
 				b1 = (int)b - (int)pal[2];
				dist = sqrt(((r1*r1)+(g1*g1)+(b1*b1)));
				if (dist < bestdist) {
					k=v;
					bestdist = dist;
				}
			}
			d_15to8table[i]=k;
		}
		snprintf(s, sizeof(s), "%s/glquake", com_gamedir);
 		Sys_mkdir (s);
		snprintf(s, sizeof(s), "%s/glquake/15to8.pal", com_gamedir);
		if ((f = fopen(s, "wb")) != NULL) {
			fwrite(d_15to8table, 1<<15, 1, f);
			fclose(f);
		}
	}
}


/*
	CheckMultiTextureExtensions

	Check for ARB, SGIS, or EXT multitexture support
*/
void
CheckMultiTextureExtensions ( void )
{
	Con_Printf ("Checking for multitexture... ");
	if (COM_CheckParm ("-nomtex"))
	{
		Con_Printf ("disabled\n");
		return;
	}
#ifdef HAVE_DLOPEN
	dlhand = dlopen (NULL, RTLD_LAZY);
	if (dlhand == NULL)
	{
		Con_Printf ("unable to check\n");
		return;
	}
	if (strstr(gl_extensions, "GL_ARB_multitexture "))
	{
		Con_Printf ("GL_ARB_multitexture\n");
		qglMTexCoord2f = (void *)dlsym(dlhand, "glMultiTexCoord2fARB");
		qglSelectTexture = (void *)dlsym(dlhand, "glActiveTextureARB");
		gl_mtex_enum = GL_TEXTURE0_ARB;
		gl_mtexable = true;
		gl_arb_mtex = true;
	} else if (strstr(gl_extensions, "GL_SGIS_multitexture "))
	{
		Con_Printf ("GL_SGIS_multitexture\n");
		qglMTexCoord2f = (void *)dlsym(dlhand, "glMTexCoord2fSGIS");
		qglSelectTexture = (void *)dlsym(dlhand, "glSelectTextureSGIS");
		gl_mtex_enum = TEXTURE0_SGIS;
		gl_mtexable = true;
		gl_arb_mtex = false;
	} else if (strstr(gl_extensions, "GL_EXT_multitexture "))
	{
		Con_Printf ("GL_EXT_multitexture\n");
		qglMTexCoord2f = (void *)dlsym(dlhand, "glMTexCoord2fEXT");
		qglSelectTexture = (void *)dlsym(dlhand, "glSelectTextureEXT");
		gl_mtex_enum = TEXTURE0_SGIS;
		gl_mtexable = true;
		gl_arb_mtex = false;
	} else {
		Con_Printf ("none found\n");
	}
	dlclose(dlhand);
	dlhand = NULL;		
#else
	gl_mtexable = false;
#endif
}


typedef void (GLAPIENTRY *gl3DfxSetDitherModeEXT_FUNC) (GrDitherMode_t mode);

/*
===============
GL_Init
===============
*/
void GL_Init (void)
{
	gl_vendor = glGetString (GL_VENDOR);
	Con_Printf ("GL_VENDOR: %s\n", gl_vendor);
	gl_renderer = glGetString (GL_RENDERER);
	Con_Printf ("GL_RENDERER: %s\n", gl_renderer);

	gl_version = glGetString (GL_VERSION);
	Con_Printf ("GL_VERSION: %s\n", gl_version);
	gl_extensions = glGetString (GL_EXTENSIONS);
	Con_Printf ("GL_EXTENSIONS: %s\n", gl_extensions);

	CheckMultiTextureExtensions ();

	glClearColor (1,0,0,0);
	glCullFace(GL_FRONT);
	glEnable(GL_TEXTURE_2D);

	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666);

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_FLAT);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	Con_Printf ("Dithering: ");

	dlhand = dlopen (NULL, RTLD_LAZY);

	if (dlhand == NULL) {
		Con_SafePrintf ("unable to set.\n");
		return;
	}

	if (strstr(gl_extensions, "3DFX_set_dither_mode")) {
		gl3DfxSetDitherModeEXT_FUNC dither_select = NULL;

		dither_select = (void *) dlsym(dlhand, "gl3DfxSetDitherModeEXT");

		if (COM_CheckParm ("-dither_2x2")) {
			dither_select(GR_DITHER_2x2);
			Con_Printf ("2x2.\n");
		} else if (COM_CheckParm ("-dither_4x4")) {
			dither_select(GR_DITHER_4x4);
			Con_Printf ("4x4.\n");
		} else {
			glDisable(GL_DITHER);
			Con_Printf ("disabled.\n");
		}
	}
	dlclose(dlhand);
	dlhand = NULL;
}

/*
=================
GL_BeginRendering

=================
*/
void GL_BeginRendering (int *x, int *y, int *width, int *height)
{
	*x = *y = 0;
	*width = scr_width;
	*height = scr_height;

//    if (!wglMakeCurrent( maindc, baseRC ))
//		Sys_Error ("wglMakeCurrent failed");

//	glViewport (*x, *y, *width, *height);
}


void GL_EndRendering (void)
{
	glFlush();
	fxMesaSwapBuffers();
	Sbar_Changed ();
}

static int resolutions[][3]={
	{ 320,	200,	GR_RESOLUTION_320x200 },
	{ 320,	240,	GR_RESOLUTION_320x240 },
	{ 400,	256,	GR_RESOLUTION_400x256 },
	{ 400,	300,	GR_RESOLUTION_400x300 },
	{ 512,	256,	GR_RESOLUTION_512x256 },
	{ 512,	384,	GR_RESOLUTION_512x384 },
	{ 640,	200,	GR_RESOLUTION_640x200 },
	{ 640,	350,	GR_RESOLUTION_640x350 },
	{ 640,	400,	GR_RESOLUTION_640x400 },
	{ 640,	480,	GR_RESOLUTION_640x480 },
	{ 800,	600,	GR_RESOLUTION_800x600 },
	{ 856,	480,	GR_RESOLUTION_856x480 },
	{ 960,	720,	GR_RESOLUTION_960x720 },
#ifdef GR_RESOLUTION_1024x768
	{ 1024,	768,	GR_RESOLUTION_1024x768 },
#endif
#ifdef GR_RESOLUTION_1152x864
	{ 1152,	864,	GR_RESOLUTION_1152x864 },
#endif
#ifdef GR_RESOLUTION_1280x960
	{ 1280,	960,	GR_RESOLUTION_1280x960 },
#endif
#ifdef GR_RESOLUTION_1280x1024
	{ 1280,	1024,	GR_RESOLUTION_1280x1024 },
#endif
#ifdef GR_RESOLUTION_1600x1024
	{ 1600,	1024,	GR_RESOLUTION_1600x1024 },
#endif
#ifdef GR_RESOLUTION_1600x1200
	{ 1600,	1200,	GR_RESOLUTION_1600x1200 },
#endif
#ifdef GR_RESOLUTION_1792x1344
	{ 1792,	1344,	GR_RESOLUTION_1792x1344 },
#endif
#ifdef GR_RESOLUTION_1856x1392
	{ 1856,	1392,	GR_RESOLUTION_1856x1392 },
#endif
#ifdef GR_RESOLUTION_1920x1440
	{ 1920,	1440,	GR_RESOLUTION_1920x1440 },
#endif
#ifdef GR_RESOLUTION_2048x1536
	{ 2048,	1536,	GR_RESOLUTION_2048x1536 },
#endif
#ifdef GR_RESOLUTION_2048x2048
	{ 2048,	2048,	GR_RESOLUTION_2048x2048 }
#endif
};

#define NUM_RESOLUTIONS		(sizeof(resolutions)/(sizeof(int)*3))


static int
findres(int *width, int *height)
{
	int i;

	for(i=0; i < NUM_RESOLUTIONS; i++) {
		if((*width <= resolutions[i][0]) &&
		   (*height <= resolutions[i][1])) {
			*width = resolutions[i][0];
			*height = resolutions[i][1];
			return resolutions[i][2];
		}
	}

	*width = 640;
	*height = 480;
	return GR_RESOLUTION_640x480;
}

qboolean VID_Is8bit(void)
{
	return is8bit;
}

typedef void (GLAPIENTRY *glColorTableEXT_FUNC) (GLenum, GLenum, GLsizei, 
		GLenum, GLenum, const GLvoid *);
typedef void (GLAPIENTRY *gl3DfxSetPaletteEXT_FUNC) (GLuint *pal);

void VID_Init8bitPalette()
{
	// Check for 8bit Extensions and initialize them.
	int i;

	dlhand = dlopen (NULL, RTLD_LAZY);

	Con_SafePrintf ("8-bit GL extensions: ");

	if (dlhand == NULL) {
		Con_SafePrintf ("unable to check.\n");
		return;
	}

	if (COM_CheckParm("-no8bit")) {
		Con_SafePrintf("disabled.\n");
		return;
	}

	if (strstr(gl_extensions, "3DFX_set_global_palette")) {
		char *oldpal;
		GLubyte table[256][4];
		gl3DfxSetPaletteEXT_FUNC load_texture = NULL;

		Con_SafePrintf("3DFX_set_global_palette.\n");
		load_texture = (void *) dlsym(dlhand, "gl3DfxSetPaletteEXT");

		glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
		oldpal = (char *) d_8to24table; //d_8to24table3dfx;
		for (i=0;i<256;i++) {
			table[i][2] = *oldpal++;
			table[i][1] = *oldpal++;
			table[i][0] = *oldpal++;
			table[i][3] = 255;
			oldpal++;
		}
		load_texture((GLuint *)table);
		is8bit = true;
	} else if (strstr(gl_extensions, "GL_EXT_shared_texture_palette")) {
		char thePalette[256*3];
		char *oldPalette, *newPalette;
		glColorTableEXT_FUNC load_texture = NULL;

		Con_SafePrintf("GL_EXT_shared.\n");
		load_texture = (void *) dlsym(dlhand, "glColorTableEXT");

		glEnable( GL_SHARED_TEXTURE_PALETTE_EXT );
		oldPalette = (char *) d_8to24table; //d_8to24table3dfx;
		newPalette = thePalette;
		for (i=0;i<256;i++) {
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			*newPalette++ = *oldPalette++;
			oldPalette++;
		}
		load_texture(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, (void *) thePalette);
		is8bit = true;
	}

	dlclose(dlhand);
	dlhand = NULL;
	Con_SafePrintf ("not found.\n");
}

void VID_Init(unsigned char *palette)
{
	int i;
	GLint attribs[32];
	char	gldir[MAX_OSPATH];
	int width = 640, height = 480;

	vid_mode = Cvar_Get ("vid_mode", "5", 0, "None");
	vid_redrawfull = Cvar_Get ("vid_redrawfull", "0", 0," None");
	vid_waitforrefresh = Cvar_Get ("vid_waitforrefresh", "0", CVAR_ARCHIVE,
			"None");

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

// interpret command-line params

// set vid parameters
	attribs[0] = FXMESA_DOUBLEBUFFER;
	attribs[1] = FXMESA_ALPHA_SIZE;
	attribs[2] = 1;
	attribs[3] = FXMESA_DEPTH_SIZE;
	attribs[4] = 1;
	attribs[5] = FXMESA_NONE;

	if ((i = COM_CheckParm("-width")) != 0)
		width = atoi(com_argv[i+1]);
	if ((i = COM_CheckParm("-height")) != 0)
		height = atoi(com_argv[i+1]);

	if ((i = COM_CheckParm("-conwidth")) != 0)
		vid.conwidth = atoi(com_argv[i+1]);
	else
		vid.conwidth = 640;

	vid.conwidth &= 0xfff8; // make it a multiple of eight

	if (vid.conwidth < 320)
		vid.conwidth = 320;

	// pick a conheight that matches with correct aspect
	vid.conheight = vid.conwidth*3 / 4;

	if ((i = COM_CheckParm("-conheight")) != 0)
		vid.conheight = atoi(com_argv[i+1]);
	if (vid.conheight < 200)
		vid.conheight = 200;

	fc = fxMesaCreateContext(0, findres(&width, &height), GR_REFRESH_75Hz,
		attribs);
	if (!fc)
		Sys_Error("Unable to create 3DFX context.\n");

	scr_width = width;
	scr_height = height;

	fxMesaMakeCurrent(fc);

	if (vid.conheight > height)
		vid.conheight = height;
	if (vid.conwidth > width)
		vid.conwidth = width;
	vid.width = vid.conwidth;
	vid.height = vid.conheight;

	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 2;

	InitSig(); // trap evil signals

	GL_Init();

	snprintf(gldir, sizeof(gldir), "%s/glquake", com_gamedir);
	Sys_mkdir (gldir);

	VID_SetPalette(palette);

	// Check for 3DFX Extensions and initialize them.
	VID_Init8bitPalette();

	Con_SafePrintf ("Video mode %dx%d initialized.\n", width, height);

	vid.recalc_refdef = 1;				// force a surface cache flush
}

void VID_ExtraOptionDraw(unsigned int options_draw_cursor)
{
/* Port specific Options menu entrys */
}

void VID_ExtraOptionCmd(int option_cursor)
{
/*
	switch(option_cursor)
	{
	case 12:  // Always start with 12
	break;
	}
*/
}
void VID_InitCvars ()
{
	gl_triplebuffer = Cvar_Get ("gl_triplebuffer","1",CVAR_ARCHIVE,"None");
}

void VID_SetCaption (char *text)
{
}

void VID_HandlePause (qboolean pause)
{
}
