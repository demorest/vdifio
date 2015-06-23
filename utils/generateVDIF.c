
#ifdef __APPLE__

#define OSX

#define OPENREADOPTIONS O_RDONLY
#define OPENWRITEOPTIONS O_WRONLY|O_CREAT|O_TRUNC

#else

#define LINUX
#define _LARGEFILE_SOURCE 
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#define OPENREADOPTIONS O_RDONLY|O_LARGEFILE
#define OPENWRITEOPTIONS O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE

#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <vdifio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <getopt.h>
#include <vdifio.h>

double currentmjd();
void mjd2cal(double mjd, int *day, int *month, int *year, double *ut);
float gaussrand();
int MeanStdDev(const float *src, int length, float *mean, float *StdDev);
int pack2bit1chan(float **in, int off, char *out, float mean, float stddev, int len);
int pack2bitNchan(float **in, int nchan, int off, char *out, float mean, float stddev, int len);
void dayno2cal (int dayno, int year, int *day, int *month);
double cal2mjd(int day, int month, int year);
double tm2mjd(struct tm date);
void generateData(float **data, int nframe, int sampesperframe, int nchan,
		  int iscomplex, int bandwidth, float tone, float *mean, float *stdDev);

#define MAXSTR        255
#define BUFSIZE        32  // MB
#define MAXPOS          3
#define SMALLPOS        2
#define SMALLNEG        1
#define MAXNEG          0

int main (int argc, char * const argv[]) {
  char *filename, *framedata, msg[MAXSTR];
  int i, n, frameperbuf, status, outfile, opt, tmp;
  uint64_t nframe;
  float **data, ftmp, stdDev, mean;
  ssize_t nr;
  vdif_header header;

  int nbits = 2;
  int bandwidth = 64;
  int nchan = 1;
  int framesize = 8000;
  int iscomplex = 0;
  int year = -1;
  int month = -1;
  int day = -1;
  int dayno = -1;
  double mjd = -1;
  float tone = 6;  // MHz
  float duration = 0; // Seconds

  struct option options[] = {
    {"bandwidth", 1, 0, 'b'},
    {"day", 1, 0, 'd'},
    {"dayno", 1, 0, 'D'},
    {"month", 1, 0, 'm'},
    {"mjd", 1, 0, 'M'},
    {"year", 1, 0, 'y'},
    {"time", 1, 0, 't'},
    {"framesize", 1, 0, 'F'},
    {"duration", 1, 0, 't'},
    {"tone", 1, 0, 'T'},
    {"nchan", 1, 0, 'n'},
    {"nthread", 1, 0, 'N'},
    {"complex", 0, 0, 'c'},
    {"help", 0, 0, 'h'},
    {0, 0, 0, 0}
  };

  srand(time(NULL));
  
  /* Read command line options */
  while (1) {
    opt = getopt_long_only(argc, argv, "b:d:m:M:y:t:n:c:T:h", options, NULL);
    if (opt==EOF) break;
    
#define CASEINT(ch,var)                                     \
  case ch:						    \
    status = sscanf(optarg, "%d", &tmp);		    \
    if (status!=1)					    \
      fprintf(stderr, "Bad %s option %s\n", #var, optarg);  \
    else                                                    \
      var = tmp;                                            \
    break

#define CASEFLOAT(ch,var)                                   \
  case ch:						    \
    status = sscanf(optarg, "%f", &ftmp);		    \
    if (status!=1)					    \
      fprintf(stderr, "Bad %s option %s\n", #var, optarg);  \
    else                                                    \
      var = ftmp;                                           \
    break
    
    switch (opt) {

      CASEINT('b', bandwidth);
      CASEINT('d', day);
      CASEINT('D', dayno);
      CASEINT('m', month);
      CASEINT('M', mjd);
      CASEINT('y', year);
      CASEINT('n', nchan);
      CASEINT('F', framesize);
      CASEFLOAT('t', duration);
      CASEFLOAT('T', tone);

      case 'c':
	iscomplex = 1;
	break;
 
      case 'h':
	printf("Usage: generateVDIF [options]\n");
	printf("  -bandwidth <BANWIDTH> Channel bandwidth in MHz (64)\n");
	printf("  -n/-nchan <N>         Number of  channels in stream\n");
	printf("  -day <DAY>            Day of month of start time (now)\n");
	printf("  -month <MONTH>        Month of start time (now)\n");
	printf("  -dayno <DAYNO>        Day of year of start time (now)\n");
	printf("  -year <YEAR>          Year of start time (now)\n");
	printf("  -time <HH:MM:SS>      Year of start time (now)\n");
	printf("  -mjd <MJD>            MJD of start time\n");
	return(1);
	break;
      
    case '?':
    default:
      break;
    }
  }
  
  if (argc==optind) {
    filename = strdup("Test.vdif");
  } else {
    filename = strdup(argv[optind]);
  }

  int cfact = 1;
  if (iscomplex) cfact = 2;

  // Frame size and number of sampler/frame
  int completesample = nchan*nbits*cfact;

  uint64_t samplerate = bandwidth*1e6*2/cfact;
  
  framesize = (framesize/8)*8; // Round framesize multiple of 8

  // need integral number of frames/sec
  uint64_t bytespersec = completesample*samplerate/8;
  // Need integral # frames/sec AND integral # completesamples/frame
  while (bytespersec % framesize != 0 && (framesize*8 % completesample !=0)) {
    framesize -=8;
    if (framesize <= 0) {
      printf("Could not select valid framesize. Aborting\n");
      printf("  nbit=%d, nchan=%d, iscomplex=%d, bandwidth=%d, completesample=%d bits\n", 
	     nbits, nchan, iscomplex, bandwidth, completesample);
      exit(1);
    }
  }
  printf("Using framesize=%d\n", framesize);
  int samplesperframe = framesize*8/completesample;
  int framespersec = bytespersec/framesize;

  printf("DEBUG: %d  %d\n", samplesperframe, framespersec);

  // Initialize memory
  data = malloc(nchan*sizeof(float*));
  if (data==NULL) {
    perror("Memory allocation problem\n");
    exit(1);
  }

  frameperbuf = BUFSIZE*1024*1024/nchan/framesize;
  printf("DEBUG: frameperbuf=%d\n", frameperbuf);

  if (duration==0) { // Just create BUFSIZE bytes
    nframe = frameperbuf;
  } else {
    nframe = duration*framespersec;
    nframe = (nframe/frameperbuf)*frameperbuf;
  }
  printf("DEBUG: Total frame = %lld\n", (long long)nframe);

  for (i=0;i<nchan;i++) {
    status = posix_memalign((void**)&data[i], 8, nframe*samplesperframe*sizeof(float)*cfact);
    if (status) {
      perror("Trying to allocate memory");
      exit(EXIT_FAILURE);
    }
  }

  status = posix_memalign((void**)&framedata, 8, framesize);
  if (status) {
    perror("Trying to allocate memory");
    exit(EXIT_FAILURE);
  }
  memset(framedata, 'Z', framesize);

  // THIS NEEDS TO MOVE TO MAIN LOOP
  // END OF MOVE LOOP
  
  double thismjd = currentmjd();
  int thisday, thismonth, thisyear;
  double ut;
  mjd2cal(thismjd, &thisday, &thismonth, &thisyear, &ut);

  if (year==-1) year = thisyear;
  if (day==-1) day = thisday;
  if (month==-1) month = thismonth;
  if (dayno!=-1) dayno2cal(dayno, year, &day, &month);
  mjd = cal2mjd(day, month, year)+ut;

  status = createVDIFHeader(&header, framesize, 0, nbits, nchan, iscomplex, "Tt");
  if (status!=VDIF_NOERROR) {
    printf("Error creating VDIF header. Aborting\n");
    exit(1);
  }
  // Whack some texture to the extended bytes
  header.extended2 = 0xAAAAAAAA;
  header.extended3 = 0xBBBBBBBB;  
  header.extended4 = 0xCCCCCCCC;

  setVDIFEpochMJD(&header, mjd);
  setVDIFFrameMJDSec(&header, (uint64_t)floor(mjd*60*60*24));
  setVDIFFrameNumber(&header,0);

  outfile = open(filename, OPENWRITEOPTIONS, S_IRWXU|S_IRWXG|S_IRWXO); 
  if (outfile==-1) {
    sprintf(msg, "Failed to open output (%s)", filename);
    perror(msg);
    exit(1);
  }
  printf("writing %s\n", filename);

  while (nframe>0) {
    printf("DEBUG: nframe=%llu\n", nframe);
    
    generateData(data, frameperbuf, samplesperframe, nchan, iscomplex, bandwidth, tone,
		 &mean, &stdDev);

    for (i=0; i<frameperbuf; i++) {

      if (nbits==2) {
	if (nchan==1) {
	  status = pack2bit1chan(data, i*samplesperframe, framedata,  mean, stdDev, samplesperframe);
	  if (status) exit(1);
	} else {
	  status = pack2bitNchan(data, nchan, i*samplesperframe, framedata,  mean, stdDev, samplesperframe);
	  if (status) exit(1);
	}
      } else {
	printf("Unsupported number of bits\n");
	exit(1);
      }
    
      nr = write(outfile, &header, VDIF_HEADER_BYTES);
      if (nr == -1) {
	sprintf(msg, "Writing to %s:", filename);
	perror(msg);
	exit(1);
      } else if (nr != VDIF_HEADER_BYTES) {
	printf("Error: Partial write to %s\n", filename);
	exit(1);
      }
    
      nr = write(outfile, framedata, framesize); 
      if (nr == -1) {
	sprintf(msg, "Writing to %s:", filename);
	perror(msg);
	exit(1);
      } else if (nr != framesize) {
	printf("Error: Partial write to %s\n", filename);
	exit(1);
      }
      nextVDIFHeader(&header, framespersec);
      nframe--;
    }
  }
  
  close(outfile);
  return(0);
}
  
float gaussrand() {
  static float V1, V2, S;
  static int phase = 0;
  float X;

  if (phase == 0) {
    do {
      float U1 = (float)rand() / RAND_MAX;
      float U2 = (float)rand() / RAND_MAX;
			
      V1 = 2 * U1 - 1;
      V2 = 2 * U2 - 1;
      S = V1 * V1 + V2 * V2;
    } while(S >= 1 || S == 0);

    X = V1 * sqrt(-2 * log(S) / S);
  } else
    X = V2 * sqrt(-2 * log(S) / S);
  
  phase = 1 - phase;
  
  return X;
}

double currentmjd () {
  struct tm *tim;
  struct timeval currenttime;

  gettimeofday(&currenttime, NULL);

  tim = localtime(&currenttime.tv_sec);
  return(tm2mjd(*tim)+(currenttime.tv_usec/1.0e6)/(24.0*60.0*60.0));
}

void mjd2cal(double mjd, int *day, int *month, int *year, double *ut) {
  int jd, temp1, temp2;

  *ut = fmod(mjd,1.0);

  if (*ut<0.0) {
    *ut += 1.0;
    mjd -= 1;
  }

  jd = (int)floor(mjd + 2400001);

  // Do some rather cryptic calculations
  
  temp1 = 4*(jd+((6*(((4*jd-17918)/146097)))/4+1)/2-37);
  temp2 = 10*(((temp1-237)%1461)/4)+5;

  *year = temp1/1461-4712;
  *month =((temp2/306+2)%12)+1;
  *day = (temp2%306)/10+1;
}

int MeanStdDev(const float *src, int length, float *mean, float *StdDev) { 
  int i;
  double sum = 0;
  double sumsqr = 0;
  for(i=0;i<length;i++) {
    sum += src[i];
    sumsqr += src[i]*src[i];
  }
  *mean = sum/length;
  *StdDev = sqrt((sumsqr-(sum*sum/length))/(length-1));
  return 0; 
}

#define F2BIT(f,j) {					\
  if(f >= maxposThresh)  /* large positive */		\
    ch[j] = MAXPOS;					\
  else if(f <= maxnegThresh) /* large negative */	\
    ch[j] = MAXNEG;					\
  else if(f > mean)  /* small positive */		\
    ch[j] = SMALLPOS;					\
  else  /* small negative */				\
    ch[j] = SMALLNEG;					\
}


int pack2bit1chan(float **in, int off, char *out, float mean, float stddev, int len) {
  int i, j, ch[4];
  float maxposThresh, maxnegThresh, *f;
  
  if (len%4!=0) {
    printf("Can only pack multiple of 4 samples!\n");
    return(1);
  }

  maxposThresh = mean+stddev*0.95;
  maxnegThresh = mean-stddev*0.95;

  f = &in[0][off];
  j = 0;
  for (i=0;i<len;) {
    F2BIT(f[i],0);
    i++;
    F2BIT(f[i],1);
    i++;
    F2BIT(f[i],2);
    i++;
    F2BIT(f[i],3);
    i++;
    out[j] = (ch[0])|((ch[1]<<2) )|((ch[2]<<4) )|((ch[3]<<6) );
    j++;
  }

  return 0;
}

int pack2bitNchan(float **in, int nchan, int off, char *out, float mean, float stddev, int len) {
  // Should check 32bit "off" offset is enough bits
  int i, j, n, k, ch[4];
  float maxposThresh, maxnegThresh;
  
  if (len*nchan%4!=0) {
    printf("Can only pack multiple of 4 samples*channels!\n");
    return(1);
  }

  maxposThresh = mean+stddev*0.95;
  maxnegThresh = mean-stddev*0.95;

  j = 0;
  k = 0;
  for (i=off;i<len+off;i++) {
    for (n=0; n<nchan; n++) {
      F2BIT(in[n][i],k);
      k++;
      if (k==4) {
	out[j] = (ch[0])|((ch[1]<<2) )|((ch[2]<<4) )|((ch[3]<<6));
	j++;
	k = 0;
      }
    }
  }
  return 0;
}



int leap (int year) {
  return (((!(year%4))&&(year%100))||(!(year%400)));
}

static int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};

void dayno2cal (int dayno, int year, int *day, int *month) {
  int end;

  if (leap(year)) {
    days[1] = 29;
  } else {
    days[1] = 28;
  }

  *month = 0;
  end = days[*month];
  while (dayno>end) {
    (*month)++;
    end+= days[*month];
  }
  end -= days[*month];
  *day = dayno - end;
  (*month)++;

  return;
}

double cal2mjd(int day, int month, int year) {
  int m, y, c, x1, x2, x3;

  if (month <= 2) {
    m = month+9;
    y = year-1;
  } else {
    m = month-3;
    y = year;
  }

  c = y/100;
  y = y-c*100;

  x1 = 146097*c/4;
  x2 = 1461*y/4;
  x3 = (153*m+2)/5;

  return(x1+x2+x3+day-678882);
}

double tm2mjd(struct tm date) {
  int y, c;
  double dayfrac;

  if (date.tm_mon < 2) {
    y = date.tm_mon+1900-1;
  } else {
    y = date.tm_year+1900;
  }

  c = y/100;
  y = y-c*100;

  dayfrac = ((date.tm_hour*60.0+date.tm_min)*60.0+date.tm_sec)/(60.0*60.0*24.0);

  return(cal2mjd(date.tm_mday, date.tm_mon+1, date.tm_year+1900)+dayfrac);
}

void generateData(float **data, int nframe, int samplesperframe, int nchan,
		  int iscomplex, int bandwidth, float tone, float *mean, float *stdDev) {
  int i, n;
  float s;

  for (i=0; i<nframe*samplesperframe; i++) {
    s = sin(tone/bandwidth*i*M_PI)*0.1; // There will be a phase jump from buffer to buffer
    for (n=0; n<nchan; n++) {
      data[n][i] = gaussrand()+s;
    }
  }

  // Calculate RMS to allow thresholding
  *mean = 0;
  *stdDev = 0;
  for (n=0; n<nchan; n++) {
    float thismean, thisStdDev;
    MeanStdDev(data[n], nframe*samplesperframe, &thismean, &thisStdDev);
    *mean += thismean;
    *stdDev += thisStdDev;
  }
  *mean /= nchan;
  *stdDev /= nchan;
}
