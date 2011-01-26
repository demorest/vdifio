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

const char program[] = "fakemultiVDIF";
const char author[]  = "Adam Deller <adeller@nrao.edu>";
const char version[] = "0.1";
const char verdate[] = "20110114";

int usage()
{
  fprintf(stderr, "\n%s ver. %s  %s  %s\n\n", program, version,
          author, verdate);
  fprintf(stderr, "A program to turn a single thread VDIF file into a fake multithread file\n");
  fprintf(stderr, "\nUsage: %s <VDIF input file> <VDIF output file> <Mbps> [-v]\n", program);
  fprintf(stderr, "\n<VDIF input file> is the name of the single thread VDIF file to read\n");
  fprintf(stderr, "\n<VDIF output file> is the name of the fake multithread VDIF file to write\n");
  fprintf(stderr, "\n<Mbps> is the data rate in Mbps expected for this input file\n");
  fprintf(stderr, "\n[-v] verbose mode on\n");
  fprintf(stderr, "The input file must at least start with one valid packet\n");

  return 0;
}

int main(int argc, char **argv)
{
  char buffer[MAX_VDIF_FRAME_BYTES*2];
  FILE * input;
  FILE * output;
  int readbytes, framebytes, framemjd, framesecond, framenumber, frameinvalid, datambps, framespersecond;
  int bufferoffset, wrotebytes, wholemissedpackets, extrareadbytes;
  int i, verbose;
  long long framesread, invalidpackets, invalidbytes;

  fprintf(stdout, "REMEMBER! The input file must be single thread!\n\n");

  if(argc != 4 && argc != 5)
    return usage();

  if(argc == 5)
    verbose = 1;
  else
    verbose = 0;

  input = fopen(argv[1], "r");
  if(input == NULL)
  {
    fprintf(stderr, "Cannot open input file %s\n", argv[1]);
    exit(1);
  }
  output = fopen(argv[2], "w");
  if(input == NULL)
  {
    fprintf(stderr, "Cannot open output file %s\n", argv[2]);
    exit(1);
  }

  datambps = atoi(argv[3]);
  invalidpackets = 0;
  invalidbytes = 0;
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
    bufferoffset = 0;
    wholemissedpackets = 0;
    extrareadbytes = 0;
    readbytes = fread(buffer, 1, framebytes, input); //read the whole VDIF packet
    if (readbytes < framebytes) {
      fprintf(stderr, "Header read failed - probably at end of file.\n");
      break;
    }

    while(getVDIFFrameBytes(buffer+bufferoffset) != framebytes) {
      //almost certainly some bogus packet.  Try looking ahead
      if(verbose)
        fprintf(stderr, "Lost the VDIF stream - skipping ahead to look for next packet\n");
      bufferoffset += 8;
      if(bufferoffset == framebytes) {
        for(i=0;i<extrareadbytes;i++)
          buffer[i] = buffer[framebytes+i];
        readbytes = fread(buffer, 1, framebytes-extrareadbytes, input); //read the whole VDIF packet
        if (readbytes < framebytes-extrareadbytes) {
          fprintf(stderr, "Header read failed - probably at end of file.\n");
          invalidpackets++;
          invalidbytes += bufferoffset + wholemissedpackets*framebytes;
          break;
        }
        bufferoffset = 0;
        extrareadbytes = 0;
        wholemissedpackets++;
      }
      if(framebytes - bufferoffset < VDIF_HEADER_BYTES) {
        readbytes = fread(buffer + VDIF_HEADER_BYTES + bufferoffset, 1, 8, input);
        extrareadbytes += 8;
      }
    }
    if(bufferoffset > 0 || wholemissedpackets > 0) {
      invalidpackets++;
      invalidbytes += bufferoffset + wholemissedpackets*framebytes;
    }
    if(bufferoffset > 0) {
      readbytes = fread(buffer+framebytes+extrareadbytes, 1, bufferoffset-extrareadbytes, input);
      if(readbytes < bufferoffset-extrareadbytes) {
        fprintf(stderr, "Header read failed - probably at end of file.\n");
        break;
      }
    }
    framemjd = getVDIFFrameMJD(buffer+bufferoffset);
    framesecond = getVDIFFrameSecond(buffer+bufferoffset);
    framenumber = getVDIFFrameNumber(buffer+bufferoffset);
    frameinvalid = getVDIFFrameInvalid(buffer+bufferoffset);
    if(getVDIFFrameBytes(buffer+bufferoffset) != framebytes) {
      fprintf(stderr, "Framebytes has changed (%d)! Can't deal with this, aborting\n", getVDIFFrameBytes(buffer+bufferoffset));
      fprintf(stderr, "Bufferoffset was %d, wholemissedpackets was %d\n", bufferoffset, wholemissedpackets);
      break;
    }
    framesread++;
    setVDIFThreadID(buffer+bufferoffset, 0);
    wrotebytes = fwrite(buffer+bufferoffset, 1, framebytes, output);
    if(wrotebytes != framebytes)
      fprintf(stderr, "Write failed!\n");
    setVDIFThreadID(buffer+bufferoffset, 1);
    wrotebytes = fwrite(buffer+bufferoffset, 1, framebytes, output);
    if(wrotebytes != framebytes)
      fprintf(stderr, "Write failed!\n");
  }

  printf("Read %lld frames, skipped over %lld dodgy packets containing %lld dodgy bytes\n", framesread, invalidpackets, invalidbytes);
  fclose(input);

  return 0;
}

