/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// draw.c -- 2d drawing

#include "quakedef.h"
#include "gl_heap.h"

cvar_t scr_conalpha = {"scr_conalpha", "0.5", CVAR_ARCHIVE}; // johnfitz

extern cvar_t rt_hud_padding;

qpic_t *draw_disc;
qpic_t *draw_backtile;

gltexture_t *char_texture;      // johnfitz
qpic_t      *pic_ovr, *pic_ins; // johnfitz -- new cursor handling
qpic_t      *pic_nul;           // johnfitz -- for missing gfx, don't crash

// johnfitz -- new pics
byte pic_ovr_data[8][8] = {
	{255, 255, 255, 255, 255, 255, 255, 255}, {255, 15, 15, 15, 15, 15, 15, 255}, {255, 15, 15, 15, 15, 15, 15, 2}, {255, 15, 15, 15, 15, 15, 15, 2},
	{255, 15, 15, 15, 15, 15, 15, 2},         {255, 15, 15, 15, 15, 15, 15, 2},   {255, 15, 15, 15, 15, 15, 15, 2}, {255, 255, 2, 2, 2, 2, 2, 2},
};

byte pic_ins_data[9][8] = {
	{15, 15, 255, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},   {15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},   {15, 15, 2, 255, 255, 255, 255, 255}, {255, 2, 2, 255, 255, 255, 255, 255},
};

byte pic_nul_data[8][8] = {
	{252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0},
	{0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252},
};

byte pic_stipple_data[8][8] = {
	{255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0}, {255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0},
	{255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0}, {255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0},
};

byte pic_crosshair_data[8][8] = {
	{255, 255, 255, 255, 255, 255, 255, 255},
	{255, 255, 255, 8, 9, 255, 255, 255},
	{255, 255, 255, 6, 8, 2, 255, 255},
	{255, 6, 8, 8, 6, 8, 8, 255},
	{255, 255, 2, 8, 8, 2, 2, 2},
	{255, 255, 255, 7, 8, 2, 255, 255},
	{255, 255, 255, 255, 2, 2, 255, 255},
	{255, 255, 255, 255, 255, 255, 255, 255},
};
// johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float        sl, tl, sh, th;
} glpic_t;

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

typedef struct cachepic_s
{
	char   name[MAX_QPATH];
	qpic_t pic;
	byte   padding[32]; // for appended glpic
} cachepic_t;

#define MAX_CACHED_PICS 512 // Spike -- increased to avoid csqc issues.
cachepic_t menu_cachepics[MAX_CACHED_PICS];
int        menu_numcachepics;

byte menuplyr_pixels[4096];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define MAX_SCRAPS   2
#define BLOCK_WIDTH  256
#define BLOCK_HEIGHT 256

int          scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte         scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT]; // johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean     scrap_dirty;
gltexture_t *scrap_textures[MAX_SCRAPS]; // johnfitz

/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;
	int texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (scrap_allocated[texnum][i + j] >= best)
					break;
				if (scrap_allocated[texnum][i + j] > best2)
					best2 = scrap_allocated[texnum][i + j];
			}
			if (j == w)
			{ // this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
	return 0;
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int  i;

	for (i = 0; i < MAX_SCRAPS; i++)
	{
		sprintf (name, "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (
			NULL,
			NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i], "", (src_offset_t)scrap_texels[i],
			TEXPREF_ALPHA | TEXPREF_OVERWRITE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags)
{
	int          i;
	cachepic_t  *pic;
	qpic_t      *p;
	glpic_t      gl;
	src_offset_t offset; // johnfitz
	lumpinfo_t  *info;

	// Spike -- added cachepic stuff here, to avoid glitches if the function is called multiple times with the same image.
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (name, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	p = (qpic_t *)W_GetLumpName (name, &info);
	if (!p)
	{
		Con_SafePrintf ("W_GetLumpName: %s not found\n", name);
		return pic_nul; // johnfitz
	}
	if (info->type != TYP_QPIC)
		Sys_Error ("Draw_PicFromWad: lump \"%s\" is not a qpic", name);
	if (info->size < (int)(sizeof (int) * 2) || sizeof (int) * 2 + p->width * p->height > (size_t)info->size)
		Sys_Error ("Draw_PicFromWad: pic \"%s\" truncated", name);
	if (p->width < 0 || p->height < 0)
		Sys_Error ("Draw_PicFromWad: bad size (%dx%d) for pic \"%s\"", p->width, p->height, name);

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int   x = 0, y = 0;
		int   j, k;
		int   texnum;
		byte *data = p->data;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i = 0; i < p->height; i++)
		{
			for (j = 0; j < p->width; j++, k++)
				scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] = data[k];
		}
		gl.gltexture = scrap_textures[texnum]; // johnfitz -- changed to an array
		// johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x / (float)BLOCK_WIDTH;
		gl.sh = (x + p->width) / (float)BLOCK_WIDTH;
		gl.tl = y / (float)BLOCK_WIDTH;
		gl.th = (y + p->height) / (float)BLOCK_WIDTH;
	}
	else
	{
		char texturename[64];                                                       // johnfitz
		q_snprintf (texturename, sizeof (texturename), "%s:%s", WADFILENAME, name); // johnfitz

		char rtname[64];
		q_snprintf (rtname, sizeof (rtname), "gfx/%s", name);

		offset = (src_offset_t)p - (src_offset_t)wad_base + sizeof (int) * 2; // johnfitz

		gl.gltexture = TexMgr_LoadImage (rtname, NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME, offset, texflags); // johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = 1;
		gl.tl = 0;
		gl.th = 1;
	}

	menu_numcachepics++;
	strcpy (pic->name, name);
	pic->pic = *p;
	memcpy (pic->pic.data, &gl, sizeof (glpic_t));

	return &pic->pic;
}

qpic_t *Draw_PicFromWad (const char *name)
{
	return Draw_PicFromWad2 (name, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
}

qpic_t *Draw_GetCachedPic (const char *path)
{
	cachepic_t *pic;
	int         i;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	return NULL;
}

/*
================
Draw_CachePic
================
*/
qpic_t *Draw_TryCachePic (const char *path, unsigned int texflags)
{
	cachepic_t *pic;
	int         i;
	qpic_t     *dat;
	glpic_t     gl;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	//
	// load the pic from disk
	//
	dat = (qpic_t *)COM_LoadFile (path, NULL);
	if (!dat)
		return NULL;
	SwapPic (dat);

	menu_numcachepics++;
	strcpy (pic->name, path);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl.gltexture = TexMgr_LoadImage (
		path, NULL, path, dat->width, dat->height, SRC_INDEXED, dat->data, path, sizeof (int) * 2, texflags | TEXPREF_NOPICMIP); // johnfitz -- TexMgr
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;

	memcpy (pic->pic.data, &gl, sizeof (glpic_t));

	Mem_Free (dat);

	return &pic->pic;
}

qpic_t *Draw_CachePic (const char *path)
{
	qpic_t *pic = Draw_TryCachePic (path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
	if (!pic)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	return pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
qpic_t *Draw_MakePic (const char *name, int width, int height, byte *data)
{
	int     flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_PAD;
	qpic_t *pic;
	glpic_t gl;

	pic = (qpic_t *)Mem_Alloc (sizeof (qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, NULL, name, width, height, SRC_INDEXED, data, "", (src_offset_t)data, flags);
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;
	memcpy (pic->data, &gl, sizeof (glpic_t));

	return pic;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
void Draw_LoadPics (void)
{
	byte        *data;
	src_offset_t offset;
	lumpinfo_t  *info;

	data = (byte *)W_GetLumpName ("conchars", &info);
	if (!data)
		Sys_Error ("Draw_LoadPics: couldn't load conchars");
	offset = (src_offset_t)data - (src_offset_t)wad_base;
	char_texture = TexMgr_LoadImage (
		"gfx/conchars",
		NULL, WADFILENAME ":conchars", 128, 128, SRC_INDEXED, data, WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t *pic;
	int         i;

	// empty scrap and reallocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty gltextures

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;

	// reload wad pics
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();
	PR_ReloadPics (false);
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&scr_conalpha);

	// clear scrap and allocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty textures

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);

	// load game pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

/*
================
Draw_FillCharacterQuad
================
*/
static void Draw_FillCharacterQuad (int x, int y, char num, RgVertex *output, int rotation)
{
	int   row, col;
	float frow, fcol, size;

	row = num >> 4;
	col = num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	RgVertex corner_verts[4] = {0};

	float texcoords[4][2] = {
		{x, y},
		{x + 8, y},
		{x + 8, y + 8},
		{x, y + 8},
	};

	corner_verts[0].position[0] = texcoords[(rotation + 0) % 4][0];
	corner_verts[0].position[1] = texcoords[(rotation + 0) % 4][1];
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texCoord[0] = fcol;
	corner_verts[0].texCoord[1] = frow;
	corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

	corner_verts[1].position[0] = texcoords[(rotation + 1) % 4][0];
	corner_verts[1].position[1] = texcoords[(rotation + 1) % 4][1];
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texCoord[0] = fcol + size;
	corner_verts[1].texCoord[1] = frow;
	corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

	corner_verts[2].position[0] = texcoords[(rotation + 2) % 4][0];
	corner_verts[2].position[1] = texcoords[(rotation + 2) % 4][1];
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texCoord[0] = fcol + size;
	corner_verts[2].texCoord[1] = frow + size;
	corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

	corner_verts[3].position[0] = texcoords[(rotation + 3) % 4][0];
	corner_verts[3].position[1] = texcoords[(rotation + 3) % 4][1];
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texCoord[0] = fcol;
	corner_verts[3].texCoord[1] = frow + size;
	corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;

	output[0] = corner_verts[0];
	output[1] = corner_verts[1];
	output[2] = corner_verts[2];
	output[3] = corner_verts[2];
	output[4] = corner_verts[3];
	output[5] = corner_verts[0];
}

/*
================
Draw_Character
================
*/
void Draw_Character (cb_context_t *cbx, int x, int y, int num)
{
	if (y <= -8)
		return; // totally off screen

	const int rotation = (num / 256) % 4;
	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	RgVertex vertices[6];
	Draw_FillCharacterQuad (x, y, (char)num, vertices, rotation);

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = RT_COLOR_WHITE,
		.material = char_texture ? char_texture->rtmaterial : RG_NO_MATERIAL,
		.pipelineState = RG_RASTERIZED_GEOMETRY_STATE_ALPHA_TEST,
		.blendFuncSrc = 0,
		.blendFuncDst = 0,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
================
Draw_String
================
*/
void Draw_String (cb_context_t *cbx, int x, int y, const char *str)
{
	int         num_verts = 0;
	int         i;
	const char *tmp;

	if (y <= -8)
		return; // totally off screen

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;
	
	RgVertex *vertices = RT_AllocScratchMemoryNulled (num_verts * sizeof (RgVertex));

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuad (x, y, *str, vertices + i * 6, 0);
			i++;
		}
		x += 8;
	}

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = num_verts,
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = RT_COLOR_WHITE,
		.material = char_texture ? char_texture->rtmaterial : RG_NO_MATERIAL,
		.pipelineState = RG_RASTERIZED_GEOMETRY_STATE_ALPHA_TEST,
		.blendFuncSrc = 0,
		.blendFuncDst = 0,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (cb_context_t *cbx, int x, int y, qpic_t *pic, float alpha, qboolean alpha_blend)
{
	glpic_t gl;

	if (scrap_dirty)
		Scrap_Upload ();
	memcpy (&gl, pic->data, sizeof (glpic_t));

	RgVertex vertices[6];
	{
		RgVertex corner_verts[4] = {0};

		corner_verts[0].position[0] = x;
		corner_verts[0].position[1] = y;
		corner_verts[0].position[2] = 0.0f;
		corner_verts[0].texCoord[0] = gl.sl;
		corner_verts[0].texCoord[1] = gl.tl;
		corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[1].position[0] = x + pic->width;
		corner_verts[1].position[1] = y;
		corner_verts[1].position[2] = 0.0f;
		corner_verts[1].texCoord[0] = gl.sh;
		corner_verts[1].texCoord[1] = gl.tl;
		corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[2].position[0] = x + pic->width;
		corner_verts[2].position[1] = y + pic->height;
		corner_verts[2].position[2] = 0.0f;
		corner_verts[2].texCoord[0] = gl.sh;
		corner_verts[2].texCoord[1] = gl.th;
		corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[3].position[0] = x;
		corner_verts[3].position[1] = y + pic->height;
		corner_verts[3].position[2] = 0.0f;
		corner_verts[3].texCoord[0] = gl.sl;
		corner_verts[3].texCoord[1] = gl.th;
		corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;

		vertices[0] = corner_verts[0];
		vertices[1] = corner_verts[1];
		vertices[2] = corner_verts[2];
		vertices[3] = corner_verts[2];
		vertices[4] = corner_verts[3];
		vertices[5] = corner_verts[0];
	}

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = {1.0f, 1.0f, 1.0f, alpha},
		.material = gl.gltexture ? gl.gltexture->rtmaterial : RG_NO_MATERIAL,
		.pipelineState = alpha_blend ? RG_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE : RG_RASTERIZED_GEOMETRY_STATE_ALPHA_TEST,
		.blendFuncSrc = alpha_blend ? RG_BLEND_FACTOR_SRC_ALPHA : 0,
		.blendFuncDst = alpha_blend ? RG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : 0,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

void Draw_SubPic (cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	glpic_t  gl;
	qboolean alpha_blend = alpha < 1.0f;
	if (alpha <= 0.0f)
		return;

	s2 += s1;
	t2 += t1;

	if (scrap_dirty)
		Scrap_Upload ();
	memcpy (&gl, pic->data, sizeof (glpic_t));
	if (!gl.gltexture)
		return;

	RgVertex vertices[6];
	{
		RgVertex corner_verts[4] = {0};

		corner_verts[0].position[0] = x;
		corner_verts[0].position[1] = y;
		corner_verts[0].position[2] = 0.0f;
		corner_verts[0].texCoord[0] = gl.sl * (1 - s1) + s1 * gl.sh;
		corner_verts[0].texCoord[1] = gl.tl * (1 - t1) + t1 * gl.th;
		corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[1].position[0] = x + w;
		corner_verts[1].position[1] = y;
		corner_verts[1].position[2] = 0.0f;
		corner_verts[1].texCoord[0] = gl.sl * (1 - s2) + s2 * gl.sh;
		corner_verts[1].texCoord[1] = gl.tl * (1 - t1) + t1 * gl.th;
		corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[2].position[0] = x + w;
		corner_verts[2].position[1] = y + h;
		corner_verts[2].position[2] = 0.0f;
		corner_verts[2].texCoord[0] = gl.sl * (1 - s2) + s2 * gl.sh;
		corner_verts[2].texCoord[1] = gl.tl * (1 - t2) + t2 * gl.th;
		corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[3].position[0] = x;
		corner_verts[3].position[1] = y + h;
		corner_verts[3].position[2] = 0.0f;
		corner_verts[3].texCoord[0] = gl.sl * (1 - s1) + s1 * gl.sh;
		corner_verts[3].texCoord[1] = gl.tl * (1 - t2) + t2 * gl.th;
		corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;
		
	    vertices[0] = corner_verts[0];
	    vertices[1] = corner_verts[1];
	    vertices[2] = corner_verts[2];
	    vertices[3] = corner_verts[2];
	    vertices[4] = corner_verts[3];
	    vertices[5] = corner_verts[0];
	}

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = {rgb[0], rgb[1], rgb[2], alpha},
		.material = gl.gltexture ? gl.gltexture->rtmaterial : RG_NO_MATERIAL,
		.pipelineState = alpha_blend ? RG_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE : RG_RASTERIZED_GEOMETRY_STATE_ALPHA_TEST,
		.blendFuncSrc = alpha_blend ? RG_BLEND_FACTOR_SRC_ALPHA : 0,
		.blendFuncDst = alpha_blend ? RG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : 0,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (cb_context_t *cbx, int x, int y, qpic_t *pic, int top, int bottom)
{
	static int oldtop = -2;
	static int oldbottom = -2;

	if (top != oldtop || bottom != oldbottom)
	{
		glpic_t p;
		memcpy (&p, pic->data, sizeof (glpic_t));
		gltexture_t *glt = p.gltexture;
		oldtop = top;
		oldbottom = bottom;
		TexMgr_ReloadImage (glt, top, bottom);
	}
	Draw_Pic (cbx, x, y, pic, 1.0f, false);
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (cb_context_t *cbx)
{
	qpic_t *pic;
	float   alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

	GL_SetCanvas (cbx, CANVAS_CONSOLE); // in case this is called from weird places

	if (alpha > 0.0)
	{
		Draw_Pic (cbx, 0, 0, pic, alpha, alpha < 1.0f);
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (cb_context_t *cbx, int x, int y, int w, int h)
{
	glpic_t gl;
	memcpy (&gl, draw_backtile->data, sizeof (glpic_t));


	RgVertex vertices[6];
	{
		RgVertex corner_verts[4] = {0};

		corner_verts[0].position[0] = x;
		corner_verts[0].position[1] = y;
		corner_verts[0].position[2] = 0.0f;
		corner_verts[0].texCoord[0] = x / 64.0;
		corner_verts[0].texCoord[1] = y / 64.0;
		corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[1].position[0] = x + w;
		corner_verts[1].position[1] = y;
		corner_verts[1].position[2] = 0.0f;
		corner_verts[1].texCoord[0] = (x + w) / 64.0;
		corner_verts[1].texCoord[1] = y / 64.0;
		corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[2].position[0] = x + w;
		corner_verts[2].position[1] = y + h;
		corner_verts[2].position[2] = 0.0f;
		corner_verts[2].texCoord[0] = (x + w) / 64.0;
		corner_verts[2].texCoord[1] = (y + h) / 64.0;
		corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[3].position[0] = x;
		corner_verts[3].position[1] = y + h;
		corner_verts[3].position[2] = 0.0f;
		corner_verts[3].texCoord[0] = x / 64.0;
		corner_verts[3].texCoord[1] = (y + h) / 64.0;
		corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;

		vertices[0] = corner_verts[0];
		vertices[1] = corner_verts[1];
		vertices[2] = corner_verts[2];
		vertices[3] = corner_verts[2];
		vertices[4] = corner_verts[3];
		vertices[5] = corner_verts[0];
	}
	
	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = RT_COLOR_WHITE,
		.material = gl.gltexture ? gl.gltexture->rtmaterial : RG_NO_MATERIAL,
		.pipelineState = RG_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE,
		.blendFuncSrc = RG_BLEND_FACTOR_SRC_ALPHA,
		.blendFuncDst = RG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (cb_context_t *cbx, int x, int y, int w, int h, int c, float alpha) // johnfitz -- added alpha
{
	byte *pal = (byte *)d_8to24table; // johnfitz -- use d_8to24table instead of host_basepal

	RgVertex vertices[6];
	{
		RgVertex corner_verts[4] = {0};

		corner_verts[0].position[0] = x;
		corner_verts[0].position[1] = y;
		corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[1].position[0] = x + w;
		corner_verts[1].position[1] = y;
		corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[2].position[0] = x + w;
		corner_verts[2].position[1] = y + h;
		corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[3].position[0] = x;
		corner_verts[3].position[1] = y + h;
		corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;

		vertices[0] = corner_verts[0];
		vertices[1] = corner_verts[1];
		vertices[2] = corner_verts[2];
		vertices[3] = corner_verts[2];
		vertices[4] = corner_verts[3];
		vertices[5] = corner_verts[0];
	}

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color =
			{
				pal[c * 4 + 0] / 255.0f,
				pal[c * 4 + 1] / 255.0f,
				pal[c * 4 + 2] / 255.0f,
				alpha,
			},
		.material = RG_NO_MATERIAL,
		.pipelineState = RG_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE,
		.blendFuncSrc = RG_BLEND_FACTOR_SRC_ALPHA,
		.blendFuncDst = RG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (cb_context_t *cbx)
{
	const float alpha = 0.5f;

	GL_SetCanvas (cbx, CANVAS_DEFAULT);

	RgVertex vertices[6];
	{
		RgVertex corner_verts[4] = {0};

		corner_verts[0].position[0] = 0.0f;
		corner_verts[0].position[1] = 0.0f;
		corner_verts[0].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[1].position[0] = glwidth;
		corner_verts[1].position[1] = 0.0f;
		corner_verts[1].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[2].position[0] = glwidth;
		corner_verts[2].position[1] = glheight;
		corner_verts[2].packedColor = RT_PACKED_COLOR_WHITE;

		corner_verts[3].position[0] = 0.0f;
		corner_verts[3].position[1] = glheight;
		corner_verts[3].packedColor = RT_PACKED_COLOR_WHITE;

		vertices[0] = corner_verts[0];
		vertices[1] = corner_verts[1];
		vertices[2] = corner_verts[2];
		vertices[3] = corner_verts[2];
		vertices[4] = corner_verts[3];
		vertices[5] = corner_verts[0];
	}

	RgRasterizedGeometryUploadInfo info = {
		.renderType = RG_RASTERIZED_GEOMETRY_RENDER_TYPE_SWAPCHAIN,
		.vertexCount = countof (vertices),
		.pVertices = vertices,
		.indexCount = 0,
		.pIndices = NULL,
		.transform = RT_TRANSFORM_IDENTITY,
		.color = {0.0f, 0.0f, 0.0f, alpha},
		.material = RG_NO_MATERIAL,
		.pipelineState = RG_RASTERIZED_GEOMETRY_STATE_BLEND_ENABLE,
		.blendFuncSrc = RG_BLEND_FACTOR_SRC_ALPHA,
		.blendFuncDst = RG_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	};

	RgResult r = rgUploadRasterizedGeometry (vulkan_globals.instance, &info, cbx->cur_viewprojection, &cbx->cur_viewport);
	RG_CHECK (r);
}

/*
================
GL_OrthoMatrix
================
*/
static void GL_OrthoMatrix (cb_context_t *cbx, float left, float right, float bottom, float top, float n, float f)
{
	float tx = -(right + left) / (right - left);
	float ty = (top + bottom) / (top - bottom);
	float tz = -(f + n) / (f - n);

	float matrix[16];
	memset (&matrix, 0, sizeof (matrix));

	// First column
	matrix[0 * 4 + 0] = 2.0f / (right - left);

	// Second column
	matrix[1 * 4 + 1] = -2.0f / (top - bottom);

	// Third column
	matrix[2 * 4 + 2] = -2.0f / (f - n);

	// Fourth column
	matrix[3 * 4 + 0] = tx;
	matrix[3 * 4 + 1] = ty;
	matrix[3 * 4 + 2] = tz;
	matrix[3 * 4 + 3] = 1.0f;

    memcpy (cbx->cur_viewprojection, matrix, 16 * sizeof (float));
}

/*
================
GL_Viewport
================
*/
void GL_Viewport (cb_context_t *cbx, float x, float y, float width, float height, float min_depth, float max_depth)
{
	RgViewport viewport;
	viewport.x = x;
	viewport.y = (float)vid.height - (y + height);
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = min_depth;
	viewport.maxDepth = max_depth;

	cbx->cur_viewport = viewport;
}

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (cb_context_t *cbx, canvastype newcanvas)
{
	if (newcanvas == cbx->current_canvas)
		return;

	float pad = CVAR_TO_INT32 (rt_hud_padding);

	extern vrect_t scr_vrect;
	float          s;
	int            lines;

	cbx->current_canvas = newcanvas;

	switch (newcanvas)
	{
	case CANVAS_NONE:
		break;
	case CANVAS_DEFAULT:
		GL_OrthoMatrix (cbx, 0, glwidth, glheight, 0, -99999, 99999);
		GL_Viewport (cbx, glx, gly, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		GL_OrthoMatrix (cbx, 0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		GL_Viewport (cbx, glx, gly, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_MENU:
		s = q_min ((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP (1.0, scr_menuscale.value, s);
		GL_OrthoMatrix (cbx, 0, 640, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glx + (glwidth - 320 * s) / 2, gly + (glheight - 200 * s) / 2, 640 * s, 200 * s, 0.0f, 1.0f);
		break;
	case CANVAS_CSQC:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_OrthoMatrix (cbx, 0, glwidth / s, glheight / s, 0, -99999, 99999);
		GL_Viewport (cbx, glx, gly, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		if (cl.gametype == GAME_DEATHMATCH)
		{
			GL_OrthoMatrix (cbx, 0, glwidth / s, 48, 0, -99999, 99999);
			GL_Viewport (cbx, glx, gly, glwidth, 48 * s, 0.0f, 1.0f);
		}
		else
		{
			GL_OrthoMatrix (cbx, 0, 320, 48, 0, -99999, 99999);
			GL_Viewport (cbx, glx + (glwidth - 320 * s) / 2, gly, 320 * s, 48 * s, 0.0f, 1.0f);
		}
		break;
	case CANVAS_SBAR_MINIMAL_BOTTOMLEFT:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_OrthoMatrix (cbx, 
			0 , 320 , 
			48, -80, 
			-99999, 99999);
		GL_Viewport (cbx, 
			glx + (s * pad), gly + (s * pad), 
			s * 320, s * 128, 
			0.0f, 1.0f);
		break;
	case CANVAS_SBAR_MINIMAL_BOTTOMRIGHT:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_OrthoMatrix (cbx, 
			0, 320, 
			48, -80, 
			-99999, 99999);
		GL_Viewport (cbx, 
			glx + (glwidth - 320 * s) - (s * pad), gly + (s * pad),
			s * 320, s * 128, 
			0.0f, 1.0f);
		break;
	case CANVAS_WARPIMAGE:
		GL_OrthoMatrix (cbx, 0, 128, 0, 128, -99999, 99999);
		GL_Viewport (cbx, glx, gly + glheight - WARPIMAGESIZE, WARPIMAGESIZE, WARPIMAGESIZE, 0.0f, 1.0f);
		break;
	case CANVAS_CROSSHAIR: // 0,0 is center of viewport
		s = CLAMP (1.0, scr_crosshairscale.value, 10.0);
		GL_OrthoMatrix (cbx, scr_vrect.width / -2 / s, scr_vrect.width / 2 / s, scr_vrect.height / 2 / s, scr_vrect.height / -2 / s, -99999, 99999);
		GL_Viewport (cbx, scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1, 0.0f, 1.0f);
		break;
	case CANVAS_BOTTOMLEFT:                // used by devstats
		s = (float)glwidth / vid.conwidth; // use console scale
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glx, gly, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	case CANVAS_BOTTOMRIGHT:               // used by fps/clock
		s = (float)glwidth / vid.conwidth; // use console scale
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glx + glwidth - 320 * s, gly, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	case CANVAS_TOPRIGHT: // used by disc
		s = 1;
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glx + glwidth - 320 * s, gly + glheight - 200 * s, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}
}
