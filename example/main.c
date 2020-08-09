/*---------------------------------------------------------------------
  Copyright (C) 1999 Nintendo.

  File   		main.c
  Comment	   	1M FLASH TEST(ver 1.25 for flash library 7.6)
  ---------------------------------------------------------------------*/
#include	<ultra64.h>
#include	"nu64sys.h"
#include	"thread.h"
#include	"graph.h"
#include  "../debugger/debugger.h"

extern u16	cfb_16_a[];
extern u16	cfb_16_b[];

u16	*cfb_tbl[2] = {
  cfb_16_a, cfb_16_b
};

#define NUM_LINES 24
#define NUM_COLS  70

char textGrid[NUM_LINES][NUM_COLS + 1];
u8 nextLineIndex;

/*
 *  Controller globals
 */
static OSMesgQueue contMessageQ;
static OSMesg   dummyMessage;
static OSContStatus statusdata[MAXCONTROLLERS];
static OSContPad controllerdata[MAXCONTROLLERS];
static int      controller;
static int      lastx;
static int      lasty;
static int      lastbutton = 0;
static u32 	seed =1;

/*
 *  Handler
 */
extern OSMesgQueue  n_dmaMessageQ;
OSPiHandle          *handler;

void println(char* text)
{
  char* nextLine = textGrid[nextLineIndex];
  char* lineEnd = nextLine + NUM_COLS;

  while (*text && nextLine < lineEnd)
  {
    *nextLine = *text;
    ++nextLine;
    ++text;
  }

  while (nextLine < lineEnd)
  {
    *nextLine = ' ';
    ++nextLine;
  }
  
  if (nextLineIndex == 0)
  {
    nextLineIndex = NUM_LINES - 1;
  }
  else
  {
    --nextLineIndex;
  }
}

int     initcontroller(void)
{
  int             i;
  u8          pattern;

  osCreateMesgQueue(&contMessageQ, &dummyMessage, 1);
  osSetEventMesg(OS_EVENT_SI, &contMessageQ, (OSMesg) 0);

  osContInit(&contMessageQ, &pattern, &statusdata[0]);

  for (i = 0; i < MAXCONTROLLERS; i++) {
    if ((pattern & (1 << i)) &&
	!(statusdata[i].errno & CONT_NO_RESPONSE_ERROR)) {
      osContStartReadData(&contMessageQ);
      controller = i;
      return i;
    }
  }
  return -1;
}

static void     readControllers(void)
{
  OSContPad      *pad;

  if (osRecvMesg(&contMessageQ, &dummyMessage, OS_MESG_NOBLOCK) == 0) {
    osContGetReadData(controllerdata);
    osContStartReadData(&contMessageQ);
  }

  pad = &controllerdata[controller];

  lastbutton = pad->button;
  lastx = pad->stick_x;
  lasty = pad->stick_y;
}

void start_display(void)
{
  int i;
  for (i = 0; i < SCREEN_WD * SCREEN_HT; i ++){
    cfb_16_a[i] = GPACK_RGBA5551(0,0,0,1);
    cfb_16_b[i] = GPACK_RGBA5551(0,0,0,1);
  } 
}

static char gTmpBuffer[0x1000];

public	void	mainproc(void *arg)
{
  u16	trig, hold;
  int	frame;

  handler = osCartRomInit();
  frame = 0;

  osViSetMode(&osViModeTable[OS_VI_NTSC_HPF1]);
  osViBlack(1);
  osViSwapBuffer( cfb_tbl[frame] );
  start_display();
  
  osViBlack(0);
  n_WaitMesg(retrace);
  initcontroller();
  gdbInitDebugger(handler, &n_dmaMessageQ);
  
  lastbutton = 0;

  while(1) {
    trig = lastbutton;
    readControllers();
    trig = lastbutton & (lastbutton & ~trig);
    hold = lastbutton;

    enum GDBDataType dataType;
    u32 chunkSize;

    while (gdbPollMessageHeader(&dataType, &chunkSize) == GDBErrorNone) {
      enum GDBError err = gdbReadMessage(gTmpBuffer, chunkSize);
      gTmpBuffer[chunkSize] = '\0';
      println(gTmpBuffer);

      if (err != GDBErrorNone) {
        println("Error reading message");
      } else {
        err = gdbSendMessage(dataType, gTmpBuffer, chunkSize);
        if (err != GDBErrorNone) {
          println("Error sending message");
        }
      }
    }

    int line;

    for (line = 0; line < NUM_LINES; ++line)
    {
      printstr(white, 5, 2 + NUM_LINES - line, textGrid[(line + nextLineIndex) % NUM_LINES]);
    }

    osWritebackDCacheAll();
    osViSwapBuffer( cfb_tbl[frame] );
    frame ^= 1;
  }
}  
