#define _XOPEN_SOURCE_EXTENDED
#include <alloca.h>
#include <locale.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <termios.h>
#include <math.h>
#include <alsa/asoundlib.h>
#include <sys/ioctl.h>
#include <fftw3.h>
#define max(a,b) \
({ __typeof__ (a) _a = (a); \
  __typeof__ (b) _b = (b); \
  _a > _b ? _a : _b; })
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <curses.h>
#include <wchar.h>


#ifdef __GNUC__
// curses.h or other sources may already define
#undef  GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif

int M = 2048;
int shared[2048];
int format = -1;
unsigned int rate = 0;

bool scientificMode = false;

struct termios oldtio, newtio;
int rc;

pid_t pacatPid;
char *path = "/tmp/mpd.fifo";
char buf[1024];

void pacat(){
  pacatPid = fork();
  if(pacatPid==0){
    snprintf(buf, sizeof buf, "pacat -r --device=alsa_output.pci-0000_00_1b.0.analog-stereo.monitor > %s", path);
    system(buf);
    memset(buf, 0, sizeof(buf));
  }
}

// general: cleanup
void cleanup()
{
  echo();
  kill(pacatPid, SIGKILL);
  system("killall pacat");
  snprintf(buf, sizeof buf, "rm %s", path);
  system(buf);
  system("setfont /usr/share/consolefonts/Lat2-Fixed16.psf.gz  >/dev/null 2>&1");
  system("setterm -blank 10");
  endwin();
  system("clear");
}

// general: handle signals
void sig_handler(int sig_no)
{
  cleanup();
  if (sig_no == SIGINT) {
    printf("CTRL-C pressed -- goodbye\n");
  }
  signal(sig_no, SIG_DFL);
  raise(sig_no);
}

//input: FIFO
void* input_fifo(void* data)
{
  int fd;
  int n = 0;
  signed char buf[1024];
  int tempr, templ, lo;
  int q, i;
  int t = 0;
  int size = 1024;
  char *path = ((char*)data);
  int bytes = 0;
  int flags;
  struct timespec req = { .tv_sec = 0, .tv_nsec = 10000000 };




  fd = open(path, O_RDONLY);
  flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  while (1) {

    bytes = read(fd, buf, sizeof(buf));

    if (bytes == -1) { //if no bytes read sleep 10ms and zero shared buffer
      nanosleep (&req, NULL);
      t++;
      if (t > 10) {
        for (i = 0; i < M; i++)shared[i] = 0;
        t = 0;
      }
    } else { //if bytes read go ahead
      t = 0;
      for (q = 0; q < (size / 4); q++) {

        tempr = ( buf[ 4 * q - 1] << 2);

        lo =  ( buf[4 * q ] >> 6);
        if (lo < 0)lo = abs(lo) + 1;
        if (tempr >= 0)tempr = tempr + lo;
        else tempr = tempr - lo;

        templ = ( buf[ 4 * q - 3] << 2);

        lo =  ( buf[ 4 * q - 2] >> 6);
        if (lo < 0)lo = abs(lo) + 1;
        if (templ >= 0)templ = templ + lo;
        else templ = templ - lo;

        shared[n] = (tempr + templ) / 2;

        n++;
        if (n == M - 1)n = 0;
      }
    }
  }
  close(fd);
}

// general: entry point
int main(int argc, char **argv)
{
  snprintf(buf, sizeof buf, "touch %s", path);
  system(buf);
  memset(buf, 0, sizeof(buf));
  pthread_t  p_thread;
  int        thr_id GCC_UNUSED;
  int om = 1;
  float fc[200];
  float fr[200];
  int lcf[200], hcf[200];
  int f[200];
  int fmem[200];
  int flast[200];
  int flastd[200];
  float peak[201];
  int y[M / 2 + 1];
  long int lpeak, hpeak;
  int bands = 25;
  int sleep = 0;
  int i, n, o, bw, height, h, w, c, rest, virt, fixedbands, q;
  int autoband = 1;
  float temp;
  double in[2 * (M / 2 + 1)];
  fftw_complex out[M / 2 + 1][2];
  fftw_plan p;
  char *color;
  int col = 6;
  int bgcol = -1;
  int sens = 100;
  int fall[200];
  float fpeak[200];
  float k[200];
  float g;
  int framerate = 60;
  float smooth[64] = {5, 4.5, 4, 3, 2, 1.5, 1.25, 1.5, 1.5, 1.25, 1.25, 1.5,
    1.25, 1.25, 1.5, 2, 2, 1.75, 1.5, 1.5, 1.5, 1.5, 1.5,
    1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5,
    1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5, 1.5,
    1.75, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
  float sm = 1.25; //min val from smooth[]
  struct timespec req = { .tv_sec = 0, .tv_nsec = 0 };
  const wchar_t* bars[] = {L"\u2581", L"\u2582", L"\u2583", L"\u2584", L"\u2585", L"\u2586", L"\u2587", L"\u2588"};
  char *usage = "\n\
  Usage : " PACKAGE " [options]\n\
  Visualize audio input in terminal. \n\
  \n\
  Options:\n\
  -b 1..(console columns/2-1) or 200	number of bars in the spectrum (default 25 + fills up the console), program will automatically adjust if there are too many frequency bands)\n\
  -p 'fifo path'				path to fifo (default '/tmp/mpd.fifo')\n\
  -c foreground color			supported colors: red, green, yellow, magenta, cyan, white, blue, black (default: cyan)\n\
  -C background color			supported colors: same as above (default: no change)\n\
  -s sensitivity				sensitivity percentage, 0% - no response, 50% - half, 100% - normal, etc...\n\
  -f framerate 				FPS limit, if you are experiencing high CPU usage, try reducing this (default: 60)\n\
  -S					\"scientific\" mode (disables most smoothing)\n\
  -h					print the usage\n\
  -v					print version\n\
  \n";
  char ch;

  setlocale(LC_ALL, "");


  for (i = 0; i < M; i++)shared[i] = 0;

  // general: handle Ctrl+C
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = &sig_handler;
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);


  // general: handle command-line arguments
  while ((c = getopt (argc, argv, "p:i:b:d:s:f:c:C:hSv")) != -1)
  switch (c) {
    case 'p': // argument: fifo path
    path = optarg;
    break;
    case 'b': // argument: bar count
    fixedbands = atoi(optarg);
    autoband = 0;
    if (fixedbands > 200)fixedbands = 200;
    break;
    case 's': // argument: sensitivity
    sens = atoi(optarg);
    break;
    case 'f': // argument: framerate
    framerate = atoi(optarg);
    if (framerate < 0) {
      fprintf(stderr,
      "framerate can't be negative!\n");
      exit(EXIT_FAILURE);
    }
    break;
    case 'c': // argument: foreground color
    col = -2;
    color = optarg;
    if (strcmp(color, "black") == 0) col = 0;
    if (strcmp(color, "red") == 0) col = 1;
    if (strcmp(color, "green") == 0) col = 2;
    if (strcmp(color, "yellow") == 0) col = 3;
    if (strcmp(color, "blue") == 0) col = 4;
    if (strcmp(color, "magenta") == 0) col = 5;
    if (strcmp(color, "cyan") == 0) col = 6;
    if (strcmp(color, "white") == 0) col = 7;
    if (col == -2) {
      fprintf(stderr, "color %s not supported\n", color);
      exit(EXIT_FAILURE);
    }
    break;
    case 'C': // argument: background color
    bgcol = -2;
    color = optarg;
    if (strcmp(color, "black") == 0) bgcol = 0;
    if (strcmp(color, "red") == 0) bgcol = 1;
    if (strcmp(color, "green") == 0) bgcol = 2;
    if (strcmp(color, "yellow") == 0) bgcol = 3;
    if (strcmp(color, "blue") == 0) bgcol = 4;
    if (strcmp(color, "magenta") == 0) bgcol = 5;
    if (strcmp(color, "cyan") == 0) bgcol = 6;
    if (strcmp(color, "white") == 0) bgcol = 7;
    if (bgcol == -2) {
      fprintf(stderr, "color %s not supported\n", color);
      exit(EXIT_FAILURE);
    }
    break;
    case 'S': // argument: enable "scientific" mode
    scientificMode = true;
    break;
    case 'h': // argument: print usage
    printf ("%s", usage);
    return 0;
    case '?': // argument: print usage
    printf ("%s", usage);
    return 1;
    case 'v': // argument: print version
    printf (PACKAGE " " VERSION "\n");
    return 0;
    default:  // argument: no arguments; exit
    abort ();
  }

  n = 0;

  pacatPid = fork();
  if(pacatPid==0){
    while(1){// Truncate the file every 20 secs
      pacat();
      system("sleep 20");
      kill(pacatPid, SIGKILL);
      system("sleep .1");
      snprintf(buf, sizeof buf, "rm %s", path);
      system(buf);
      memset(buf, 0, sizeof(buf));
      snprintf(buf, sizeof buf, "touch %s", path);
      system(buf);
      memset(buf, 0, sizeof(buf));
    }
  }

  // input: wait for the input to be ready
  thr_id = pthread_create(&p_thread, NULL, input_fifo,
  (void*)path); //starting fifomusic listener
  rate = 44100;
  format = 16;

  p =  fftw_plan_dft_r2c_1d(M, in, *out, FFTW_MEASURE); //planning to rock



  //output: start ncurses mode
  virt = system("setfont cava.psf  >/dev/null 2>&1");
  if (virt == 0) system("setterm -blank 0");
  initscr();
  curs_set(0);
  timeout(0);
  noecho();
  start_color();
  use_default_colors();
  init_pair(1, col, bgcol);
  if(bgcol != -1)
  bkgd(COLOR_PAIR(1));
  attron(COLOR_PAIR(1));
  attron(A_BOLD);



  while  (1) {//jumbing back to this loop means that you resized the screen
    for (i = 0; i < 200; i++) {
      flast[i] = 0;
      flastd[i] = 0;
      fall[i] = 0;
      fpeak[i] = 0;
      fmem[i] = 0;
    }



    //getting orignial numbers of bands incase of resize
    if (autoband == 1)  {
    bands = 25;
    } else bands = fixedbands;


    // output: get terminal's geometry
    clear();
    getmaxyx(stdscr,h,w);

    if (bands > COLS / 2 - 1)bands = COLS / 2 -
    1; //handle for user setting to many bars

    if (bands < 1) bands = 1; // must have at least 1 bar;

    height = LINES - 1;

    bw = (COLS - bands - 1) / bands;

    if (bw < 1) bw = 1; //bars must have width

    // process [smoothing]: calculate gravity
    g = ((float)height / 400) * pow((60 / (float)framerate), 2.5);

    //if no bands are selected it tries to padd the default 20 if there is extra room
    if (autoband == 1) bands = bands + ((COLS - (bw * bands + bands - 1)) /
    (bw + 1));


    //checks if there is stil extra room, will use this to center
    rest = (COLS - bands * bw - bands + 1) / 2;
    if (rest < 0)rest = 0;

    #ifdef DEBUG
    printw("hoyde: %d bredde: %d bands:%d bandbredde: %d rest: %d\n",
    COLS,
    LINES, bands, bw, rest);
    #endif

    // process: calculate cutoff frequencies
    for (n = 0; n < bands + 1; n++) {
      fc[n] = 10000 * pow(10, -2.37 + ((((float)n + 1) / ((float)bands + 1)) *
      2.37)); //decided to cut it at 10k, little interesting to hear above
      fr[n] = fc[n] / (rate /
        2); //remember nyquist!, pr my calculations this should be rate/2 and  nyquist freq in M/2 but testing shows it is not... or maybe the nq freq is in M/4
        lcf[n] = fr[n] * (M /
          4); //lfc stores the lower cut frequency foo each band in the fft out buffer

      if (n != 0) {
        hcf[n - 1] = lcf[n] - 1;
        if (lcf[n] <= lcf[n - 1])lcf[n] = lcf[n - 1] +
        1; //pushing the spectrum up if the expe function gets "clumped"
        hcf[n - 1] = lcf[n] - 1;
      }

      #ifdef DEBUG
      if (n != 0) {
        printw("%d: %f -> %f (%d -> %d) \n", n, fc[n - 1], fc[n], lcf[n - 1],
        hcf[n - 1]);
      }
      #endif
    }

    // process: weigh signal to frequencies
    for (n = 0; n < bands;
      n++)k[n] = pow(fc[n],0.62) * ((float)height/(M*3000))  * 8;



    // general: main loop
    while  (1) {

      // general: keyboard controls

      ch = getch();
      switch (ch) {
        case 65:    // key up
        sens += 10;
        break;
        case 66:    // key down
        sens -= 10;
        break;
        case 67:    // key right
        break;
        case 68:    // key left
        break;
        case 's':
        scientificMode = !scientificMode;
        break;
        case 'q':
        cleanup();
        return EXIT_SUCCESS;
      }



      // output: check if terminal has been resized
      if (virt != 0) {
        if ( LINES != h || COLS != w) {
          break;
        }
      }

      #ifdef DEBUG
      system("clear");
      #endif

      // process: populate input buffer and check if input is present
      lpeak = 0;
      hpeak = 0;
      for (i = 0; i < (2 * (M / 2 + 1)); i++) {
        if (i < M) {
          in[i] = shared[i];
          if (shared[i] > hpeak) hpeak = shared[i];
          if (shared[i] < lpeak) lpeak = shared[i];
        } else in[i] = 0;
      }
      peak[bands] = (hpeak + abs(lpeak));
      if (peak[bands] == 0)sleep++;
      else sleep = 0;

      // process: if input was present for the last 5 seconds apply FFT to it
      if (sleep < framerate * 5) {

        // process: send input to external library
        fftw_execute(p);

        // process: separate frequency bands
        for (o = 0; o < bands; o++) {
          peak[o] = 0;

          // process: get peaks
          for (i = lcf[o]; i <= hcf[o]; i++) {
            y[i] = pow(pow(*out[i][0], 2) + pow(*out[i][1], 2), 0.5); //getting r of compex
            peak[o] += y[i]; //adding upp band
          }
          peak[o] = peak[o] / (hcf[o]-lcf[o]+1); //getting average
          temp = peak[o] * k[o] * ((float)sens / 100); //multiplying with k and adjusting to sens settings
          if (temp > height * 8)temp = height * 8; //just in case
          f[o] = temp;


        }

      } else { //**if in sleep mode wait and continue**//
        #ifdef DEBUG
        printw("no sound detected for 3 sec, going to sleep mode\n");
        #endif
        //wait 1 sec, then check sound again.
        req.tv_sec = 1;
        req.tv_nsec = 0;
        nanosleep (&req, NULL);
        continue;
      }

      // process [smoothing]
      if (!scientificMode)
      {

        // process [smoothing]: falloff
        for (o = 0; o < bands; o++) {
          temp = f[o];

          if (temp < flast[o]) {
            f[o] = fpeak[o] - (g * fall[o] * fall[o]);
            fall[o]++;
          } else if (temp >= flast[o]) {
            f[o] = temp;
            fpeak[o] = f[o];
            fall[o] = 0;
          }

          flast[o] = f[o];
        }

        // process [smoothing]: monstercat-style "average"
        int z, m_y;
        float m_o = 64 / bands;
        for (z = 0; z < bands; z++) {
          f[z] = f[z] * sm / smooth[(int)floor(z * m_o)];
          if (f[z] < 0.125)f[z] = 0.125;
          for (m_y = z - 1; m_y >= 0; m_y--) {
            f[m_y] = max(f[z] / pow(2, z - m_y), f[m_y]);
          }
          for (m_y = z + 1; m_y < bands; m_y++) {
            f[m_y] = max(f[z] / pow(2, m_y - z), f[m_y]);
          }
        }

        // process [smoothing]: integral
        for (o = 0; o < bands; o++) {
          fmem[o] = fmem[o] * 0.55 + f[o];
          f[o] = fmem[o];

          if (f[o] < 1)f[o] = 1;

          #ifdef DEBUG
          mvprintw(o,0,"%d: f:%f->%f (%d->%d)peak:%f adjpeak: %f \n", o, fc[o], fc[o + 1],
          lcf[o], hcf[o], peak[o], f[o]);
          #endif
        }
      }

      // output: draw processed input
      #ifndef DEBUG
      switch (om) {
        case 1:

        for (i = 0; i <  bands; i++) {

          if(f[i] > flastd[i]){//higher then last one
            if (virt == 0) for (n = flastd[i] / 8; n < f[i] / 8; n++) for (q = 0; q < bw; q++) mvprintw((height - n), (i * bw) + q + i + rest, "%d",8);
            else for (n = flastd[i] / 8; n < f[i] / 8; n++) for (q = 0; q < bw; q++) mvaddwstr((height - n), (i * bw) + q + i + rest, bars[7]);
            if (f[i] % 8 != 0) {
              if (virt == 0) for (q = 0; q < bw; q++) mvprintw( (height - n), (i * bw) + q + i + rest, "%d",(f[i] % 8) );
              else for (q = 0; q < bw; q++) mvaddwstr( (height - n), (i * bw) + q + i + rest, bars[(f[i] % 8) - 1]);
            }
          }else if(f[i] < flastd[i]){//lower then last one
            for (n = f[i] / 8; n < flastd[i]/8 + 1; n++) for (q = 0; q < bw; q++) mvaddstr( (height - n), (i*bw) + q + i + rest, " ");
            if (f[i] % 8 != 0) {
              if (virt == 0) for (q = 0; q < bw; q++) mvprintw((height - f[i] / 8), (i * bw) + q + i + rest, "%d",(f[i] % 8) );
              else for (q = 0; q < bw; q++) mvaddwstr((height - f[i] / 8), (i * bw) + q + i + rest, bars[(f[i] % 8) - 1]);
            }
          }
          flastd[i] = f[i]; //memmory for falloff func
        }

        refresh();
        break;
      }


      if (framerate <= 1) {
        req.tv_sec = 1  / (float)framerate;
      } else {
        req.tv_sec = 0;
        req.tv_nsec = (1 / (float)framerate) * 1000000000; //sleeping for set us
      }

      nanosleep (&req, NULL);
      #endif
    }
  }
}
