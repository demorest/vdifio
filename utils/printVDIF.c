/***************************************************************************
 *   Copyright (C) 2010 by Adam Deller                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
/*===========================================================================
 * SVN properties (DO NOT CHANGE)
 *
 * $Id: stripVDIF.c 2006 2010-03-04 16:43:04Z AdamDeller $
 * $HeadURL:  $
 * $LastChangedRevision: 2006 $
 * $Author: AdamDeller $
 * $LastChangedDate: 2010-03-04 09:43:04 -0700 (Thu, 04 Mar 2010) $
 *
 *==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include "vdifio.h"

const char program[] = "printVDIF";
const char author[]  = "Adam Deller <adeller@nrao.edu>";
const char version[] = "0.1";
const char verdate[] = "20100217";

int usage()
{
  fprintf(stderr, "\n%s ver. %s  %s  %s\n\n", program, version,
          author, verdate);
  fprintf(stderr, "A program to dump some basic info about VDIF packets to the screen\n");
  fprintf(stderr, "\nUsage: %s <VDIF input file> <Mbps>\n", program);
  fprintf(stderr, "\n<VDIF input file> is the name of the VDIF file to read\n");
  fprintf(stderr, "\n<Mbps> is the data rate in Mbps expected for this file\n");

  return 0;
}

int main(int argc, char **argv)
{
  int VERBOSE = 0;
  char buffer[MAX_VDIF_FRAME_BYTES];
  FILE * input;
  int readbytes, framebytes, framemjd, framesecond, framenumber, frameinvalid, datambps, framespersecond;
  int packetdropped;
  long long framesread;

  if(argc != 3)
    return usage();
  
  input = fopen(argv[1], "r");
  if(input == NULL)
  {
    fprintf(stderr, "Cannot open input file %s\n", argv[1]);
    exit(1);
  }

  datambps = atoi(argv[2]);
  readbytes = fread(buffer, 1, VDIF_HEADER_BYTES, input); //read the VDIF header
  framebytes = getVDIFFrameBytes(buffer);
  if(framebytes > MAX_VDIF_FRAME_BYTES) {
    fprintf(stderr, "Cannot read frame with %d bytes > max (%d)\n", framebytes, MAX_VDIF_FRAME_BYTES);
    exit(1);
  }
  framemjd = getVDIFFrameMJD(buffer);
  framesecond = getVDIFFrameSecond(buffer);
  framenumber = getVDIFFrameNumber(buffer);
  frameinvalid = getVDIFFrameInvalid(buffer);
  framespersecond = (int)((((long long)datambps)*1000000)/(8*(framebytes-VDIF_HEADER_BYTES)));
  printf("Frames per second is %d\n", framespersecond);
 
  fseek(input, 0, SEEK_SET); //go back to the start

  framesread = 0;
  while(!feof(input)) {
    packetdropped = 0;
    readbytes = fread(buffer, 1, framebytes, input); //read the whole VDIF packet
    if (readbytes < framebytes) {
      fprintf(stderr, "Header read failed - probably at end of file.\n");
      break;
    }
    framemjd = getVDIFFrameMJD(buffer);
    framesecond = getVDIFFrameSecond(buffer);
    framenumber = getVDIFFrameNumber(buffer);
    frameinvalid = getVDIFFrameInvalid(buffer);
    printf("MJD is %d, second is %d, framenumber is %d, frameinvalid is %d\n", framemjd, framesecond, framenumber, frameinvalid);
    if(getVDIFFrameBytes(buffer) != framebytes) { 
      fprintf(stderr, "Framebytes has changed! Can't deal with this, aborting\n");
      break;
    }
    framesread++;
  }

  printf("Read %lld %lld frames\n", framesread);
  fclose(input);
}
