
#include <unistd.h>
#include <png.h>
#include <string.h>
#include <stdarg.h>
#include <cstdlib>
#include "MemoryLCD.h"
#include <iostream>
using namespace std;

/*
compile with

g++ -o lcd_png lcd_png.cpp MemoryLCD.cpp -l bcm2835 -l pthread -l png -I.

*/

  /*
   * The Raspberry Pi GPIO pins used for SPI are:
   * P1-19 (MOSI)
   * P1-21 (MISO)
   * P1-23 (CLK)
   * P1-24 (CE0)
   * P1-26 (CE1)
   */

// use the pin name/number, not the number based on physical header position
char SCS       = 23;      // Use any pin except the dedicated SPI SS pins?
char DISP      = 24;      // Use any non-specialised GPIO pin
char EXTCOMIN  = 25;	     // Use any non-specialised GPIO pin

MemoryLCD memLcd(SCS, DISP, 0, true);

#define LCD_ROWS 240
#define LCD_COLUMNS 400

static int hasSuffix(const char* str, const char* suffix)
{
	int stlen = strlen(str);
	int sflen = strlen(suffix);

	return (stlen >= sflen) && (strcmp(str + stlen - sflen, suffix) == 0);
}

static void drawSolid(int byte);
static void drawData(uint8_t* data, int invert);
static uint8_t* loadImage(const char* path, char** outerr);

int main(int argc, const char* argv[])
{
	enum { kNone, kWhite, kBlack, kImage, kImageInvert } action = kNone;
	const char* file = NULL;
	uint8_t* imagedata = NULL;

	if ( argc > 1 )
	{
		if ( strcmp(argv[1], "black") == 0 )
			action = kBlack;
		else if ( strcmp(argv[1], "white") == 0 )
			action = kWhite;
		else
		{
			const char* file = NULL;

			if ( strcmp(argv[1], "-i") == 0 && argc > 2 && hasSuffix(argv[2], ".png") )
			{
				action = kImageInvert;
				file = argv[2];
			}
			else if ( hasSuffix(argv[1], ".png") )
			{
				action = kImage;
				file = argv[1];
			}

			if ( file != NULL )
			{
				char* err;
				imagedata = loadImage(file, &err);

				if ( imagedata == NULL )
				{
					printf("%s\n", err);
					exit(-1);
				}
			}
		}
	}

	if ( action == kNone )
	{
		printf("usage:\n");
		printf("	%s black: paints the screen black\n", argv[0]);
		printf("	%s white: paints the screen white\n", argv[0]);
		printf("	%s [-i] <image.png>: draws the given PNG image to the screen, optionally inverting the image\n", argv[0]);
		exit(-1);
	}

	memLcd.clearDisplay();

	for ( int i = 0; i < 2; ++i )
	{
		if ( action == kWhite )
			drawSolid(0xff);
		else if ( action == kBlack )
			drawSolid(0x00);
		else
			drawData(imagedata, action == kImageInvert ? 1 : 0);

		memLcd.softToggleVCOM();
	}

	memLcd.turnOff();

	return 0;
}

static void drawSolid(int byte)
{
	for ( int y = 1; y <= LCD_ROWS; ++y)
	{
		for ( int x = 1; x <= LCD_COLUMNS / 8; ++x )
			memLcd.writeByteToLineBuffer(x, byte);

		memLcd.writeLineBufferToDisplay(y);
		memLcd.clearLineBuffer();
	}
}

static void drawData(uint8_t* data, int invert)
{
	for ( int y = 0; y < LCD_ROWS; ++y)
	{
		for ( int x = 0; x < LCD_COLUMNS / 8; ++x )
			memLcd.writeByteToLineBuffer(x+1, *data++ ^ (invert ? 0xff : 0x00));

		memLcd.writeLineBufferToDisplay(y+1);
		memLcd.clearLineBuffer();
	}
}

static char* aprintf(const char* fmt, ...)
{
	char* msg;
	va_list argp;
	va_start(argp, fmt);
	vasprintf(&msg, fmt, argp);
	va_end(argp);
	return msg;
}

static uint8_t* loadImage(const char* path, char** outerr)
{
	png_image pngimage; /* The control structure used by libpng */

	/* Initialize the 'png_image' structure. */
	memset(&pngimage, 0, (sizeof pngimage));
	pngimage.version = PNG_IMAGE_VERSION;

	/* The first argument is the file to read: */
	if ( png_image_begin_read_from_file(&pngimage, path) == 0 )
	{
		*outerr = aprintf("png_image_begin_read_from_file() failed for %s: %s", path, pngimage.message);
		return NULL;
	}

	pngimage.format = PNG_FORMAT_RGBA;

	png_bytep buffer = (png_bytep)malloc(PNG_IMAGE_SIZE(pngimage));

	if ( png_image_finish_read(&pngimage, NULL/*background*/, buffer, 0/*row_stride*/, NULL/*colormap*/) == 0 )
	{
		*outerr = aprintf("png_image_finish_read() failed for %s: %s", path, pngimage.message);
		return NULL;
	}

	int width = pngimage.width;
	int height = pngimage.height;

	if ( width < LCD_COLUMNS || height < LCD_ROWS )
	{
		*outerr = aprintf("image %s is too small (%i x %i)", path, width, height);
		return NULL;
	}

	uint8_t* data = (uint8_t*)malloc(LCD_ROWS * LCD_COLUMNS / 8);
	uint8_t* p = data;

	for ( int y = 0; y < LCD_ROWS; ++y )
	{
		png_bytep inrow = &buffer[y*width*4];
		uint8_t byte = 0;

		for ( int x = 0; x < LCD_COLUMNS; ++x )
		{
			if ( ((int)inrow[4*x] + (int)inrow[4*x+1] + (int)inrow[4*x+2]) / 3 > 0x7f )
				byte |= (0x80 >> (x%8));

			if ( x % 8 == 7 )
			{
				*p++ = byte;
				byte = 0;
			}
		}
	}

	return data;
}
