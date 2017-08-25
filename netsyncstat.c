/**************************************************************
 * check synchronization of clocks in a network
 * 
 * Created by: Tom Coen
 * Credit: https://stackoverflow.com/questions/35568996/socket-programming-udp-client-server-in-c
           https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
           https://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x
           https://stackoverflow.com/questions/5444197/converting-host-to-ip-by-sockaddr-in-gethostname-etc

On stdout the listener prints average time_diff and standard deviation (calculated as /N, NOT /(N-1)) in ns
 **************************************************************/
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h> // fixed size types
#include <stdbool.h> // bool type
#include <sys/socket.h> // Needed for socket creating and binding
#include <netinet/in.h> // Needed to use struct sockaddr_in
#include <string.h> // memset
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <netdb.h>

#ifdef __MACH__  // for mac. Can remove...
#include <mach/clock.h>
#include <mach/mach.h>
#endif

int listener(unsigned int port, unsigned int count, double interval_s, char *out_file);

int transmitter(unsigned int port, unsigned int count, double interval_s, char *hostname);

int
main (int argc, char **argv)
{ 
  bool transmitterFlag = false;
  double interval_s = 1.0;
  int count = 5;
  int port = 5777;
  char *out_file = "/dev/null";
  char *address;

  int aflag = 0;
  int bflag = 0;
  char *cvalue = NULL;
  int index;
  int c;


  opterr = 0;

  while ((c = getopt (argc, argv, "t:p:n:i:o:")) != -1)
    switch (c)
      {
      case 't':
        transmitterFlag = true;
        address = optarg;
        break;
      case 'p':
        port = atoi(optarg);
        break;
      case 'n':
        count = atoi(optarg);
        break;
      case 'o':
        out_file = optarg;
        break;
      case 'i':
        interval_s = atof(optarg);
        break;
      case '?':
        if (optopt == 'p' || optopt == 'n' || optopt == 'i')
          fprintf (stderr, "Option -%c requires an argument.\n", optopt);
        else if (isprint (optopt))
          fprintf (stderr, "Unknown option `-%c'.\n", optopt);
        else
          fprintf (stderr,
                   "Unknown option character `\\x%x'.\n",
                   optopt);
        return 1;
      default:
        abort ();
      }


  printf ("transmitter = %d, port = %d, interval = %f, count = %d\n",
          transmitterFlag, port, interval_s, count);


  
  int err;
  if (transmitterFlag) {
    err = transmitter( port, count, interval_s, address);
  }
  else { 
    err = listener(port, count, interval_s, out_file);
  }

  if (err) fprintf (stderr, "There was an error. Code %d\n", err);

}

int transmitter(unsigned int port, unsigned int count, double interval_s, char *hostname) {
  struct hostent        *he;


  int fd;
  if ( (fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket failed");
        return 1;
  }

  struct sockaddr_in serveraddr;
  memset( &serveraddr, 0, sizeof(serveraddr) );
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons( (uint32_t) port );  

  if ( (he = gethostbyname(hostname) ) == NULL ) {
      exit(1); /* error */
  }
  memcpy(&serveraddr.sin_addr, he->h_addr_list[0], he->h_length);            
  
  char str[40];
  for ( int i = 0; i < count; i++ ) {
    struct timespec ts;
    int bytes;

#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    ts.tv_sec = mts.tv_sec;
    ts.tv_nsec = mts.tv_nsec;
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    bytes = snprintf(str,40,"%lu %lu", ts.tv_sec, ts.tv_nsec); // to do binary

    if (sendto( fd, str, bytes, 0, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0 ) {
      perror( "sendto failed" );
      break;
    }
    printf( "message sent %d bytes\n", bytes);
    usleep((useconds_t) (1e6*interval_s) );
  }

  close( fd );
  
  return 0;
}

int listener(unsigned int port, unsigned int count, double interval_s, char *out_file) {
  int fd;
  uint64_t *time_diff_ns;
  struct timespec now;

  if ( (fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
    perror( "socket failed" );
    return 1;
  }
    
  time_diff_ns = malloc(sizeof *time_diff_ns * count);
  struct sockaddr_in serveraddr;
  memset( &serveraddr, 0, sizeof(serveraddr) );
  serveraddr.sin_family = AF_INET;
  serveraddr.sin_port = htons( port );
  serveraddr.sin_addr.s_addr = htonl( INADDR_ANY );

  if ( bind(fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0 ) {
    perror( "bind failed" );
    return 1;
  }

  char buffer[200];
  for ( int i = 0; i < count; i++ ) {
    int length = recvfrom( fd, buffer, sizeof(buffer) - 1, 0, NULL, 0 );
    if ( length < 0 ) {
      perror( "recvfrom failed" );
      break;
    }
#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    clock_serv_t cclock;
    mach_timespec_t mts;
    host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
    clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);
    now.tv_sec = mts.tv_sec;
    now.tv_nsec = mts.tv_nsec;
#else
    clock_gettime(CLOCK_REALTIME, &now);
#endif
    buffer[length] = '\0';
    printf( "%d bytes: '%s'\n", length, buffer ); 
    uint64_t msg_secs, msg_nsecs, now_nsecs;
    sscanf(buffer, "%llu %llu", &msg_secs, &msg_nsecs); // to do binary
    msg_nsecs = msg_secs*1000000000 + msg_nsecs;
    now_nsecs = now.tv_sec*1000000000 + now.tv_nsec;
    time_diff_ns[i] = now_nsecs-msg_nsecs;
    printf( "time diff = %llu\n", time_diff_ns[i] ); 
  }

  close( fd );

  // do statistics
  double sum_ns=0;
  double sum_squared_ns=0;
  double average_ns, stddev_ns;
  for (int i=0; i<count;i++)
  {
    sum_ns += time_diff_ns[i];
    sum_squared_ns += time_diff_ns[i]*time_diff_ns[i];
  }
  average_ns = sum_ns/count;
  stddev_ns = sqrt(sum_squared_ns/(count) - average_ns*average_ns);
  printf("##\n%f\n%f\n", average_ns, stddev_ns);

  FILE *file = fopen(out_file, "w");
  if (file != NULL) {
    for (int i=0; i<count;i++)
    {
      fprintf(file, "%llu\n", time_diff_ns[i]);
    }
  }
  fclose(file);
  return 0;
}