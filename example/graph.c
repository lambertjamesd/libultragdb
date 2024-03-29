/*====================================================================
  File  : graph.c

  Created   by Koji Mitsunari.    Aug,20 1996.
  Copyright by Nintendo, Co., Ltd.       1996.
  ====================================================================*/
#include	<ultra64.h>
#include	"nu64sys.h"
#include	"graph.h"

extern  u8      pat[][CHAR_HT];

u16	__color;

/*---------------------------------------------------------------------
                  Display Big character on the screen
  ---------------------------------------------------------------------*/
void bigputc(u16 color, int curs_x, int curs_y, char c, int xsize, int ysize)
{
  int	i, j, k, l, x, y;
  u16	*p;

  x = curs_x*CHAR_WD;
  p = (u16 *)osViGetNextFramebuffer();

  if ( ysize ) {
    y = curs_y*CHAR_HT;
    
    for (j = 0; j < CHAR_HT; j ++) {
      for (i = 0; i < 8; i ++) {
	if (pat[c-0x20][j] & (1 << (8-i))) {
	  for (k = 0; k < xsize; k ++){
	    for (l = 0; l < ysize; l ++){
	      p[x + SCREEN_WD*(y-8+l) + k] = color;
	    }
	  }
	} else {
#if	0
	  for (k = 0; k < xsize; k ++){
	    for (l = 0; l < ysize; l ++){
	    	p[x + SCREEN_WD*(y-8+l) + k] = BGCOLOR;
	    }
	  }
#endif
	}
	x += xsize;
      }
      x = curs_x*CHAR_WD;
      y += ysize;
    }
  } else {
    y = curs_y*CHAR_HT/2;

    for (j = 0; j < CHAR_HT; j += 2) {
      for (i = CHAR_WD - 1; i >= 0; i --) {
	if (pat[c-0x20][j] & (1 << i)) {
	  p[x + SCREEN_WD*(y-8)] = color;
#if	0
	} else {
	  p[x + SCREEN_WD*(y-8)] = BGCOLOR;
#endif
	}
	x++;
      }
      x = curs_x*CHAR_WD;
      y ++;
    }
  }
}

/*---------------------------------------------------------------------
                  Display big character string on the screen
  ---------------------------------------------------------------------*/
void printbig(u16 color, int curs_x, int curs_y, char *s, int xsize, int ysize)
{
    int i;

    for (i = 0 ; *s != NULL ; i ++){
      bigputc(color, curs_x + i*xsize, curs_y, *s++, xsize, ysize);
    }
}

/*---------------------------------------------------------------------
                  Display character on the screen
  ---------------------------------------------------------------------*/
void putchr(u16 color, int curs_x, int curs_y, char c)
{
  int	i, j;
  int	x = curs_x*CHAR_WD;
  int	y = curs_y*CHAR_HT;
  u8	*pc;
  if (c < 0x20) {
    pc = pat[0];
  } else {
    pc = pat[c-0x20];
  }
  u16	*p = (u16 *)osViGetNextFramebuffer() + x + SCREEN_WD*(y-8);

  for (j = 0; j < CHAR_HT; j ++, pc++) {
    for (i = CHAR_WD - 1; i >= 0; i --) {
      if (*pc & (1 << i)) {
	      *p = color;
      } else {
	      *p = BGCOLOR;
      }
      p ++;
    }
    p += SCREEN_WD - CHAR_WD;
  }
}

/*---------------------------------------------------------------------
                  Display reverse character on the screen
  ---------------------------------------------------------------------*/
void revchar(u16 color, int curs_x, int curs_y, char c)
{
  int	i, j;
  int	x = curs_x*CHAR_WD;
  int	y = curs_y*CHAR_HT;
  u8	*pc = pat[c-0x20];
  u16	*p = (u16 *)osViGetNextFramebuffer() + x + SCREEN_WD*(y-8);

  for (j = 0; j < CHAR_HT; j ++, pc++) {
    for (i = CHAR_WD - 1; i >= 0; i --) {
      if (*pc & (1 << i)) {
	*p = BGCOLOR;
      } else {
	*p = color;
      }
      p ++;
    }
    p += SCREEN_WD - CHAR_WD;
  }
}

/*---------------------------------------------------------------------
                  Display hex-data on the screen
  ---------------------------------------------------------------------*/
void putint_h(u16 color, int curs_x, int curs_y, int num, char c) {
  int	 i, k;
    
  for (i = 0 ; i < c ; i ++) {
    k = num % 16;
    if (k > 9) {
      putchr(color, curs_x - i, curs_y, k + 'A' - 10);
    } else {
      putchr(color, curs_x - i, curs_y, k + '0');
    }
    num = num >> 4;
  }    
}

/*---------------------------------------------------------------------
                  Display dec-data on the screen
  ---------------------------------------------------------------------*/
void putint_d(u16 color, int curs_x, int curs_y, int i)
{
  char	c;
  int	j;
    
  if (i >= 0) {
    c = ' ';
    j = i;
  } else {
    c = '-';
    j = -i;
  }
  
  do {
    putchr(color, curs_x--, curs_y, (j % 10) + '0');
    j /= 10;
  } while (j > 0);

  putchr(color, curs_x-1, curs_y, ' ');
  putchr(color, curs_x, curs_y, c); 

  if (i > -10 && i < 10){
    putchr(color, curs_x-2, curs_y, ' '); 
  }
}

/*---------------------------------------------------------------------
                  Display string on the screen
  ---------------------------------------------------------------------*/
void printstr(u16 color, int curs_x, int curs_y, char *s)
{
    int i;

    for (i = 0 ; *s != NULL; i ++) {
      putchr(color, curs_x + i, curs_y, *s++);
    }
}

/*---------------------------------------------------------------------
                  Put a dot on the screen
  ---------------------------------------------------------------------*/
void pset(int x, int y, u16 color)
{
  ((u16 *)osViGetNextFramebuffer())[x + SCREEN_WD*y]=color;
}

/*---------------------------------------------------------------------
                  Make a circle on the screen
  ---------------------------------------------------------------------*/
void circle(int x0, int y0, int r, u16 color)
{
  int	x, y, f;

  x = r;
  y = 0;
  f = -2 * r + 3;
  
  while (x >= y) {
    pset(x0 + x, y0 + y, color);
    pset(x0 - x, y0 + y, color);
    pset(x0 + x, y0 - y, color);
    pset(x0 - x, y0 - y, color);
    pset(x0 + y, y0 + x, color);
    pset(x0 - y, y0 + x, color);
    pset(x0 + y, y0 - x, color);
    pset(x0 - y, y0 - x, color);
    if (f >= 0) {
      x --;
      f -= x*4;
    }
    y++;
    f += 4*y + 2;
  }
}

/*---------------------------------------------------------------------
                  Make a line on the screen
  ---------------------------------------------------------------------*/
void line(int x0, int y0, int x1, int y1, u16 color)
{
  int	dx, dy, sx, sy, i, e;
  int	 x = x0;
  int	 y = y0;

  if (x1-x0 > 0){
    sx = 1;
    dx = x1-x0;
  } else if (x1-x0 < 0) {
    sx = -1;
    dx = x0-x1;
  } else {
    sx = 0;
    dx = 0;
  }

  if (y1-y0 > 0){
    sy = 1;
    dy = y1-y0;
  } else if (y1-y0 < 0) {
    sy = -1;
    dy = y0-y1;
  } else {
    sy = 0;
    dy = 0;
  }

  if (dx >= dy) {
    e = -dx;
    for (i=0; i<= dx; i++){
      pset(x, y, color);
      x += sx;
      e += 2*dy;
      if (e>=0) {
	y += sy;
	e -= 2*dx;
      }
    }
  } else {
    e = -dy;
    for (i = 0; i <= dy; i ++){
      pset(x, y, color);
      y += sy;
      e += 2*dx;
      if (e >= 0) {
	x += sx;
	e -= 2*dy;
      }
    }
  }    
} 

/*---------------------------------------------------------------------
                  Make a box on the screen
  ---------------------------------------------------------------------*/
void box(int x0, int y0, int x1, int y1, u16 color)
{
  line(x0, y0, x1, y0, color);
  line(x0, y0, x0, y1, color);
  line(x0, y1, x1, y1, color);
  line(x1, y0, x1, y1, color);
}

/*---------------------------------------------------------------------
                  Clear the screen
  ---------------------------------------------------------------------*/
void gcls(void)
{
  int 	i;
  u16	*p;
  
  p = (u16 *)osViGetNextFramebuffer();
  for (i = 0; i < SCREEN_WD*SCREEN_HT; i ++) {
#if	BMP_DATA
    *p++ = bmpdata[i];
#else
    *p++ = BGCOLOR;
#endif
  }
}

/*---------------------------------------------------------------------
                  Set Graphic data to tmp buffer 
  ---------------------------------------------------------------------*/
void SetBG(u8 *gr, u16 *gbuf)
{
  int 	i, j;

  for (i = 0, j = 0 ; i < SCREEN_WD*SCREEN_HT ; i ++ , j += 3) {
    *gbuf++ = GPACK_RGBA5551(gr[j],  gr[j + 1], gr[j + 2], 1);
  }
}

/*---------------------------------------------------------------------
                  Write Graphic data to buffer from tmp buffer
  ---------------------------------------------------------------------*/
void WriteBG(u8 *gr, u16 *gbuf)
{
  int 	i;
  u16	*p;

  p = (u16 *)osViGetNextFramebuffer();
  for (i = 0 ; i < SCREEN_WD*SCREEN_HT ; i ++ ) {
    *p++ = *gbuf++;
  }
}

/*---------------------------------------------------------------------
                  Display character on the tmp buffer
  ---------------------------------------------------------------------*/
void gputchar(u16 color, int curs_x, int curs_y, char c, u16 *buf)
{
  int	i, j;
  int	x = curs_x*CHAR_WD;
  int	y = curs_y*CHAR_HT;
  u8	*pc = pat[c-0x20];
  u16	*p = buf + x + SCREEN_WD*(y-8);

  for (j = 0; j < CHAR_HT; j ++, pc++) {
    for (i = CHAR_WD - 1; i >= 0; i --) {
      if (*pc & (1 << i)) {
	*p = color;
      } else {
	*p = BGCOLOR;
      }
      p ++;
    }
    p += SCREEN_WD - CHAR_WD;
  }
}

/*---------------------------------------------------------------------
                  Display string on the tmp buffer
  ---------------------------------------------------------------------*/
void gprintstr(u16 color, int curs_x, int curs_y, char *s, u16 *buf)
{
    int i;

    for (i = 0; *s != NULL; i ++) {
      gputchar(color, curs_x+i, curs_y, *s++, buf);
    }
}


