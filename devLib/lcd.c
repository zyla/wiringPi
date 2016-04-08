/*
 * lcd.c:
 *	Text-based LCD driver.
 *	This is designed to drive the parallel interface LCD drivers
 *	based in the Hitachi HD44780U controller and compatables.
 *
 * Copyright (c) 2012 Gordon Henderson.
 ***********************************************************************
 * This file is part of wiringPi:
 *	https://projects.drogon.net/raspberry-pi/wiringpi/
 *
 *    wiringPi is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    wiringPi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public License
 *    along with wiringPi.  If not, see <http://www.gnu.org/licenses/>.
 ***********************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <wiringPi.h>
#include <wiringPiI2C.h>

#include "lcd.h"

#ifndef	TRUE
#  define	TRUE	(1==1)
#  define	FALSE	(1==2)
#endif

// HD44780U Commands

#define	LCD_CLEAR	0x01
#define	LCD_HOME	0x02
#define	LCD_ENTRY	0x04
#define	LCD_CTRL	0x08
#define	LCD_CDSHIFT	0x10
#define	LCD_FUNC	0x20
#define	LCD_CGRAM	0x40
#define	LCD_DGRAM	0x80

// Bits in the entry register

#define	LCD_ENTRY_SH		0x01
#define	LCD_ENTRY_ID		0x02

// Bits in the control register

#define	LCD_BLINK_CTRL		0x01
#define	LCD_CURSOR_CTRL		0x02
#define	LCD_DISPLAY_CTRL	0x04

// Bits in the function register

#define	LCD_FUNC_F	0x04
#define	LCD_FUNC_N	0x08
#define	LCD_FUNC_DL	0x10

#define	LCD_CDSHIFT_RL	0x04

#define LCD_BACKLIGHT(lcd) ((lcd)->backlight_state? 1 << (lcd)->backlight_bit: 0)

struct lcdDataStruct
{
  int bits, rows, cols ;
  int rsPin, strbPin ;
  int dataPins [8] ;
  int cx, cy ;
  int i2c_fd;
  int backlight_bit;
  int backlight_state;
} ;

struct lcdDataStruct *lcds [MAX_LCDS] ;

static int lcdControl ;

// Row offsets

static const int rowOff [4] = { 0x00, 0x40, 0x14, 0x54 } ;


/*
 * strobe:
 *	Toggle the strobe (Really the "E") pin to the device.
 *	According to the docs, data is latched on the falling edge.
 *********************************************************************************
 */

static void strobe (const struct lcdDataStruct *lcd)
{

// Note timing changes for new version of delayMicroseconds ()

  digitalWrite (lcd->strbPin, 1) ; delayMicroseconds (50) ;
  digitalWrite (lcd->strbPin, 0) ; delayMicroseconds (50) ;
}


static void i2c_send (const struct lcdDataStruct *lcd, unsigned char output)
{
  wiringPiI2CWrite(lcd->i2c_fd, output | (1 << lcd->strbPin) | LCD_BACKLIGHT(lcd));
  delayMicroseconds(50);
  wiringPiI2CWrite(lcd->i2c_fd, output | LCD_BACKLIGHT(lcd));
  delayMicroseconds(50);
}

static unsigned char marshal4Bits(const struct lcdDataStruct *lcd, unsigned char data) {
  unsigned char i;
  unsigned char myData = data ;
  unsigned char output = 0;

  for (i = 0 ; i < 4 ; ++i)
  {
    output |= (myData & 1) << lcd->dataPins[i];
    myData >>= 1 ;
  }

  return output;
}


/*
 * sentDataCmd:
 *	Send an data or command byte to the display.
 *********************************************************************************
 */

static void sendDataCmd (const struct lcdDataStruct *lcd, unsigned char data, unsigned char rs)
{
  register unsigned char myData = data ;
  unsigned char          i, d4 ;

  if(lcd->i2c_fd) {

    i2c_send(lcd, marshal4Bits(lcd, (data >> 4) & 0xf) | (rs << lcd->rsPin));
    i2c_send(lcd, marshal4Bits(lcd,  data       & 0xf) | (rs << lcd->rsPin));

  } else {
    digitalWrite (lcd->rsPin, rs) ;

    if (lcd->bits == 4)
    {
      d4 = (myData >> 4) & 0x0F;
      for (i = 0 ; i < 4 ; ++i)
      {
        digitalWrite (lcd->dataPins [i], (d4 & 1)) ;
        d4 >>= 1 ;
      }
      strobe (lcd) ;

      d4 = myData & 0x0F ;
      for (i = 0 ; i < 4 ; ++i)
      {
        digitalWrite (lcd->dataPins [i], (d4 & 1)) ;
        d4 >>= 1 ;
      }
    }
    else
    {
      for (i = 0 ; i < 8 ; ++i)
      {
        digitalWrite (lcd->dataPins [i], (myData & 1)) ;
        myData >>= 1 ;
      }
    }
    strobe (lcd) ;
  }
}


/*
 * putCommand:
 *	Send a command byte to the display
 *********************************************************************************
 */

static void putCommand (const struct lcdDataStruct *lcd, unsigned char command)
{
  sendDataCmd  (lcd, command, 0) ;
  delay (2) ;
}

static void put4Command (const struct lcdDataStruct *lcd, unsigned char command)
{
  register unsigned char myCommand = command ;
  register unsigned char i ;

  if(lcd->i2c_fd) {
    i2c_send(lcd, marshal4Bits(lcd, command));
  } else {
    digitalWrite (lcd->rsPin,   0) ;

    for (i = 0 ; i < 4 ; ++i)
    {
      digitalWrite (lcd->dataPins [i], (myCommand & 1)) ;
      myCommand >>= 1 ;
    }
    strobe (lcd) ;
  }
}


/*
 *********************************************************************************
 * User Callable code below here
 *********************************************************************************
 */

/*
 * lcdHome: lcdClear:
 *	Home the cursor or clear the screen.
 *********************************************************************************
 */

void lcdHome (const int fd)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  putCommand (lcd, LCD_HOME) ;
  lcd->cx = lcd->cy = 0 ;
  delay (5) ;
}

void lcdClear (const int fd)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  putCommand (lcd, LCD_CLEAR) ;
  putCommand (lcd, LCD_HOME) ;
  lcd->cx = lcd->cy = 0 ;
  delay (5) ;
}


/*
 * lcdDisplay: lcdCursor: lcdCursorBlink:
 *	Turn the display, cursor, cursor blinking on/off
 *********************************************************************************
 */

void lcdDisplay (const int fd, int state)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  if (state)
    lcdControl |=  LCD_DISPLAY_CTRL ;
  else
    lcdControl &= ~LCD_DISPLAY_CTRL ;

  putCommand (lcd, LCD_CTRL | lcdControl) ; 
}

void lcdCursor (const int fd, int state)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  if (state)
    lcdControl |=  LCD_CURSOR_CTRL ;
  else
    lcdControl &= ~LCD_CURSOR_CTRL ;

  putCommand (lcd, LCD_CTRL | lcdControl) ; 
}

void lcdCursorBlink (const int fd, int state)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  if (state)
    lcdControl |=  LCD_BLINK_CTRL ;
  else
    lcdControl &= ~LCD_BLINK_CTRL ;

  putCommand (lcd, LCD_CTRL | lcdControl) ; 
}


/*
 * lcdSendCommand:
 *	Send any arbitary command to the display
 *********************************************************************************
 */

void lcdSendCommand (const int fd, unsigned char command)
{
  struct lcdDataStruct *lcd = lcds [fd] ;
  putCommand (lcd, command) ;
}


/*
 * lcdPosition:
 *	Update the position of the cursor on the display.
 *	Ignore invalid locations.
 *********************************************************************************
 */

void lcdPosition (const int fd, int x, int y)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  if ((x > lcd->cols) || (x < 0))
    return ;
  if ((y > lcd->rows) || (y < 0))
    return ;

  putCommand (lcd, x + (LCD_DGRAM | rowOff [y])) ;

  lcd->cx = x ;
  lcd->cy = y ;
}


/*
 * lcdCharDef:
 *	Defines a new character in the CGRAM
 *********************************************************************************
 */

void lcdCharDef (const int fd, int index, unsigned char data [8])
{
  struct lcdDataStruct *lcd = lcds [fd] ;
  int i ;

  putCommand (lcd, LCD_CGRAM | ((index & 7) << 3)) ;

  for (i = 0 ; i < 8 ; ++i)
    sendDataCmd (lcd, data [i], 1) ;
}


/*
 * lcdPutchar:
 *	Send a data byte to be displayed on the display. We implement a very
 *	simple terminal here - with line wrapping, but no scrolling. Yet.
 *********************************************************************************
 */

void lcdPutchar (const int fd, unsigned char data)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  sendDataCmd  (lcd, data, 1) ;

  if (++lcd->cx == lcd->cols)
  {
    lcd->cx = 0 ;
    if (++lcd->cy == lcd->rows)
      lcd->cy = 0 ;
    
    putCommand (lcd, lcd->cx + (LCD_DGRAM | rowOff [lcd->cy])) ;
  }
}


/*
 * lcdPuts:
 *	Send a string to be displayed on the display
 *********************************************************************************
 */

void lcdPuts (const int fd, const char *string)
{
  while (*string)
    lcdPutchar (fd, *string++) ;
}


/*
 * lcdPrintf:
 *	Printf to an LCD display
 *********************************************************************************
 */

void lcdPrintf (const int fd, const char *message, ...)
{
  va_list argp ;
  char buffer [1024] ;

  va_start (argp, message) ;
    vsnprintf (buffer, 1023, message, argp) ;
  va_end (argp) ;

  lcdPuts (fd, buffer) ;
}

extern int  lcdNew (const struct lcd_config *config)
{
  static int initialised = 0 ;

  int i ;
  int lcdFd = -1 ;
  struct lcdDataStruct *lcd ;

  if (initialised == 0)
  {
    initialised = 1 ;
    for (i = 0 ; i < MAX_LCDS ; ++i)
      lcds [i] = NULL ;
  }

// Simple sanity checks

  if (! ((config->bits == 4) || (config->bits == 8)))
    return -1 ;

  if ((config->rows < 0) || (config->rows > 20))
    return -1 ;

  if ((config->cols < 0) || (config->cols > 20))
    return -1 ;

// Create a new LCD:

  for (i = 0 ; i < MAX_LCDS ; ++i)
  {
    if (lcds [i] == NULL)
    {
      lcdFd = i ;
      break ;
    }
  }

  if (lcdFd == -1)
    return -1 ;

  lcd = (struct lcdDataStruct *)malloc (sizeof (struct lcdDataStruct)) ;
  if (lcd == NULL)
    return -1 ;

  lcd->rsPin   = config->rs ;
  lcd->strbPin = config->strb ;
  lcd->bits    = config->bits ;
  lcd->rows    = config->rows ;
  lcd->cols    = config->cols ;
  lcd->cx      = 0 ;
  lcd->cy      = 0 ;

  lcd->dataPins [0] = config->d0 ;
  lcd->dataPins [1] = config->d1 ;
  lcd->dataPins [2] = config->d2 ;
  lcd->dataPins [3] = config->d3 ;
  lcd->dataPins [4] = config->d4 ;
  lcd->dataPins [5] = config->d5 ;
  lcd->dataPins [6] = config->d6 ;
  lcd->dataPins [7] = config->d7 ;

  lcd->backlight_bit = config->backlight;
  lcd->backlight_state = config->backlight_state;

  if(config->i2c_addr) {
    lcd->i2c_fd = wiringPiI2CSetup(config->i2c_addr);
    // TODO error handling
  } else {
    lcd->i2c_fd = 0;
  }

  lcds [lcdFd] = lcd ;

  if(lcd->i2c_fd) {
    wiringPiI2CWrite(lcd->i2c_fd, LCD_BACKLIGHT(lcd));
  } else {
    digitalWrite (lcd->rsPin,   0) ; pinMode (lcd->rsPin,   OUTPUT) ;
    digitalWrite (lcd->strbPin, 0) ; pinMode (lcd->strbPin, OUTPUT) ;

    for (i = 0 ; i < lcd->bits ; ++i)
    {
      digitalWrite (lcd->dataPins [i], 0) ;
      pinMode      (lcd->dataPins [i], OUTPUT) ;
    }
  }
  delay (35) ; // mS

  return lcdFd ;
}

void lcdReinit(int lcdFd) {
  unsigned char func ;
  struct lcdDataStruct *lcd;
  lcd = lcds[lcdFd];

// 4-bit mode?
//	OK. This is a PIG and it's not at all obvious from the documentation I had,
//	so I guess some others have worked through either with better documentation
//	or more trial and error... Anyway here goes:
//
//	It seems that the controller needs to see the FUNC command at least 3 times
//	consecutively - in 8-bit mode. If you're only using 8-bit mode, then it appears
//	that you can get away with one func-set, however I'd not rely on it...
//
//	So to set 4-bit mode, you need to send the commands one nibble at a time,
//	the same three times, but send the command to set it into 8-bit mode those
//	three times, then send a final 4th command to set it into 4-bit mode, and only
//	then can you flip the switch for the rest of the library to work in 4-bit
//	mode which sends the commands as 2 x 4-bit values.

  if (lcd->bits == 4)
  {
    func = LCD_FUNC | LCD_FUNC_DL ;			// Set 8-bit mode 3 times
    put4Command (lcd, func >> 4) ; delay (35) ;
    put4Command (lcd, func >> 4) ; delay (35) ;
    put4Command (lcd, func >> 4) ; delay (35) ;
    func = LCD_FUNC ;					// 4th set: 4-bit mode
    put4Command (lcd, func >> 4) ; delay (35) ;
  }
  else
  {
    func = LCD_FUNC | LCD_FUNC_DL ;
    putCommand  (lcd, func     ) ; delay (35) ;
    putCommand  (lcd, func     ) ; delay (35) ;
    putCommand  (lcd, func     ) ; delay (35) ;
  }

  if (lcd->rows > 1)
  {
    func |= LCD_FUNC_N ;
    putCommand (lcd, func) ; delay (35) ;
  }

// Rest of the initialisation sequence

  lcdDisplay     (lcdFd, TRUE) ;
  lcdCursor      (lcdFd, FALSE) ;
  lcdCursorBlink (lcdFd, FALSE) ;
  lcdClear       (lcdFd) ;

  putCommand (lcd, LCD_ENTRY   | LCD_ENTRY_ID) ;
  putCommand (lcd, LCD_CDSHIFT | LCD_CDSHIFT_RL) ;
}

/*
 * lcdInit:
 *	Take a lot of parameters and initialise the LCD, and return a handle to
 *	that LCD, or -1 if any error.
 *********************************************************************************
 */

int lcdInit (const struct lcd_config *config)
{
  int lcdFd = lcdNew(config);

  if(lcdFd < 0)
      return -1;

  lcdReinit(lcdFd);

  return lcdFd;
}

/*
 * lcdBacklight:
 * Change backlight state of the display
 *********************************************************************************
 */

void lcdBacklight(const int fd, int state)
{
  struct lcdDataStruct *lcd = lcds [fd] ;

  lcd->backlight_state = state;

  if(lcd->i2c_fd) {
    wiringPiI2CWrite(lcd->i2c_fd, LCD_BACKLIGHT(lcd));
  }
}
