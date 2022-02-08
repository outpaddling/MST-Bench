/***************************************************************************
 * Description:
 *      System benchmark utility.
 *      
 *      This utility is designed to assess best possible performance
 *      that can be realized by real software running on a given
 *      platform (hardware, operating system, and compiler).
 *
 *      Some benchmarks attempt to measure raw hardware performance,
 *      while others attempt to predict performance of specific
 *      types of applications (games, databases, floating point
 *      computation).
 *
 *      The goal of this benchmark is to show the maximum performance
 *      of basic subsystems such as memory and disk transfer rate
 *      that you can expect to see from *real software* running
 *      on this system.
 *
 *      For example, the best sustained disk
 *      transfer rate that can be achieved by software is well below
 *      the hardware transfer rate advertized by the disk manufacturer.
 *      Actual disk transfer rates also depend on CPU speed, memory speed,
 *      bus speed, software efficiency, and how much processing the
 *      program must do between successive read/write operations.
 *      If software is not ready to read or write at the moment the
 *      appropriate block passed under the read/write head, it will
 *      have to wait up to another complete disk rotation (7.8ms on
 *      a 7200 RPM disk).  Hence, a small delay in processing could
 *      have a major impact on the actual disk transfer rate.
 *
 *      Similarly, the amount of data that a program can read from
 *      or write to memory in a given time is slowed from the hardware
 *      specs by loop and data processing overhead.
 *
 *      This benchmark will *NOT* predict the performance of any particular
 *      application running on the same system.  However, it should
 *      provide a reasonable prediction of the average performance
 *      of all applications.
 *
 *      Most programs will not realize the performance reported by
 *      this benchmark.  The benchmark is meant to show the practical
 *      limits of performance, which real software can be measured against
 *      to determine efficiency and utilization.
 *
 *      Major factors affecting performance:
 *
 *      Small array: (mostly cache access)
 *          Cache response time, internal bus speed and width, cpu speed
 *      Large array: (cache overwhelmed, mostly external RAM)
 *          DRAM speed, external bus speed and width, cpu speed
 *      Disk sequential read/write:
 *          Disk transfer rate, disk seek time,
 *          filesystem (UFS, EXT, NTFS, etc.)
 *      Disk random read/write:
 *          Disk seek time, filesystem, disk transfer rate
 *
 * Returns:
 *      0 upon successful completion, non-zero error codes otherwise.
 *
 * History: 
 *      Sep 2009    J Bacon
 ***************************************************************************/

#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>

#if defined(_BORLAND)

#include <bios.h>
#include <io.h>
#include <malloc.h>

struct timeval
{
    long            tv_sec;
    long            tv_usec;
};

long    difftimeofday(struct timeval *, struct timeval *);
int     gettimeofday(struct timeval *, void *);

#elif defined(_WIN32)   // Visual C++

#include <windows.h>
#include <time.h>
#include <io.h>

#define open    _open
#define write   _write
#define lseek   _lseek
#define read    _read
#define close   _close

#else

#include <unistd.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#endif

#include "protos.h"

#define REPORT_FORMAT   "%12s %-7s %12s blocks %12.2f ms %12.2f MiB/s\n"

/*
 *  File size should be larger than physical memory so that buffers
 *  will be overwhelmed and disk performance will actually be measured.
 */

#define KIBI            1024L
#define MEBI            (KIBI*KIBI)
#define GIBI            (MEBI*KIBI)

#define BLOCK_SIZE      (MEBI*4L)
#define BLOCKS          (file_size/BLOCK_SIZE)

// #define SEEKS           8192L
#define SEEKS           BLOCKS

#define SMALL_ARRAY_SIZE    (128L*KIBI)
#define SMALL_ARRAY_REPS    (256L*KIBI)
#define ARRAY_REPS          40L

/*
 *  Should divide into BLOCKS and SEEKS for a clean progress bar
 *  One asterisk will be printed for each iteration where
 *  BLOCKS % BAR_WIDTH == 0 or SEEKS % BAR_WIDTH == 0
 */
#define BAR_WIDTH       64

#define TEMP_FILE       "mst-bench.tempfile"

#define MS_PER_SEC      1000
#define US_PER_MS       1000

#define MSG_MAX         100

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

int     main(int argc, char *argv[])

{
    int             trial,
		    trials = 3;
    unsigned long   cache[4] = {0,0,0,0},
		    array[4] = {0,0,0,0},
		    write = 0,
		    seek = 0,
		    read = 0,
		    rewrite = 0,
		    c,
		    wsize;
    struct utsname  un;
    size_t          mem_size,
		    array_size;
    uint64_t        file_size;
    char            host[_POSIX_HOST_NAME_MAX+1];

    //BSD only:
#ifdef __APPLE__
    char    discard[100];
    unsigned long   mem_size_ul;
    FILE            *fp;
    
    /* Always returns mem_size=0 (Snow Leopard, Xcode 3.2.6)
    unsigned long mem_size_len;
    if ( sysctlbyname("hw.physmem", &mem_size, &mem_size_len, NULL, 0) != 0 )
	perror("sysctlbyname() failed");
    */
    
    /* Temporary hack */
    fp = popen("sysctl hw.physmem", "r");
    fscanf(fp, "%s %lu", discard, &mem_size_ul);
    mem_size = mem_size_ul;
    pclose(fp);
#else
    mem_size = sysconf(_SC_PHYS_PAGES);
    
    /* sysconf() returns long, so do separately to avoid overflow */
    mem_size *= sysconf(_SC_PAGE_SIZE);
#endif

    /* Overwhelm RAM buffers. */
    /* CHANGEME: Allow command-line override */
    file_size = mem_size * 2LL;

    /* Process command line */
    for (c = 0; c < argc; ++c)
    {
	if ( strcmp(argv[c], "--help") == 0 )
	    usage(argv);
	else if ( strcmp(argv[c], "--trials") == 0 )
	{
	    trials = atoi(argv[++c]);
	}
	else if ( strcmp(argv[c], "--file-size") == 0 )
	{
	    file_size = atoi(argv[++c]) * GIBI;
	}
    }
    
    puts("\nThis test should be run on a completely idle system.");
    puts("Make sure the load average near 0, and I/O activity");
    puts("is nonexistant.  Single user mode is the best option.");
    puts("\nPress return to begin...");
    getchar();
    
    gethostname(host, _POSIX_HOST_NAME_MAX);
    printf("Hostname =\t%s\n", host);
    
    /* Report OS, compiler, filesystem */
    uname(&un);
    
    printf("System =\t%s %s %s\nCompiler =\t%s\nRAM =\t\t%lu MiB\n",
	un.sysname, un.release, un.machine, COMPILER, mem_size / 1048576);
// FIXME: Add equivalents for other platforms
#ifdef __FreeBSD__
    putchar('\n');
    system("grep '^CPU:' /var/run/dmesg.boot");
    system("grep '/SMP:' /var/run/dmesg.boot");
#endif
    printf("\nFile size =\t%qu MiB\n",
	(long long unsigned int)file_size / 1048576);
    
    printf("CWD =\t\t%s\n", getcwd(NULL,0));
    printf("Date/time =\t");
    fflush(stdout);
    system("date");

// FIXME: Add equivalents for other platforms
#ifdef __FreeBSD__
    puts("\nDisk hardware:\n");
    system("geom disk list");
    system("df | fgrep -q /dev/mfid && mfiutil show adapter");
    putchar('\n');
    system("df | fgrep -q /dev/mfid && mfiutil show config");
#endif

    puts("\nMount options:\n");
    system("/sbin/mount");
    
    puts("\nDisk free:\n");
    system("/bin/df -h");
    
    system("mount | fgrep 'zfs,' && zpool status");
    putchar('\n');
    
    /* Overwhelm cache, but don't cause paging */
    array_size = MIN(mem_size * 3 / 4, 512 * MEBI);
    
    /* Ensure different random results each time */
    srandom(time(NULL));
    
    for (trial=1; trial<=trials; ++trial)
    {
	puts("===========================================================");
	printf("Trial %d...\n", trial);
	puts("===========================================================");
	
	/* Memory tests */
	puts("Testing small array, high cache hit-ratio...");
	for (c = 0, wsize = 1; c < 4; ++c, wsize *= 2)
	    cache[c] += array_test(SMALL_ARRAY_SIZE, SMALL_ARRAY_REPS, wsize);
	puts("Testing large array, low cache hit-ratio...");
	for (c = 0, wsize = 1; c < 4; ++c, wsize *= 2)
	    array[c] += array_test(array_size, ARRAY_REPS, wsize);
	
	/* Disk tests */
	write += write_test_low(file_size);
	read += read_test_low(file_size);
	rewrite += write_test_low(file_size);
	seek += seek_test(file_size);
	unlink(TEMP_FILE);
    }
    
    /* Compute and report averages */
    printf("Averages of %d trials:\n", trials);
    /*printf("%20s %12s %12s %18s\n", "Test", "Block size", "Time (ms)",
	"Throughput (MiB/s)");*/
    
    for (c=0, wsize=1; c < 4; ++c, wsize*=2)
    {
	cache[c] /= trials;
	report_throughput(cache[c], "array",
	    SMALL_ARRAY_SIZE, SMALL_ARRAY_REPS, 
	    wsize);
    }

    for (c=0, wsize=1; c < 4; ++c, wsize*=2)
    {
	array[c] /= trials;
	report_throughput(array[c], "array", array_size, ARRAY_REPS,
	    wsize);
    }
    
    write /= trials;
    seek /= trials;
    read /= trials;
    rewrite /= trials;
    
    report_throughput(write, "write", file_size, 1, BLOCK_SIZE);
    report_throughput(read, "read", file_size, 1, BLOCK_SIZE);
    report_throughput(rewrite, "rewrite", file_size, 1, BLOCK_SIZE);
    report_throughput(seek, "seek", file_size, 1, BLOCK_SIZE);
    return EX_OK;
}


unsigned long   array_test(unsigned long array_size, int reps, int wsize)

{
    uint64_t        *array = malloc(array_size),
		    *qp;;
    uint32_t        *lp;
    uint16_t        *sp;
    uint8_t         *bp,
		    *end;
    unsigned long   milliseconds,
		    c;
    struct timeval  start_time, end_time;
    char            array_size_str[MSG_MAX+1];
    
    if ( array == NULL )
    {
	fprintf(stderr, "Could not allocate %lu bytes.\n", array_size);
	exit(1);
    }
    
    end = (uint8_t *)array + array_size;
    
    /* Test CPU and cache speed */
    printf("Filling a %s array %u times %d bytes at a time...\n",
	    size_str(array_size, array_size_str, NULL), reps, wsize);
    gettimeofday(&start_time, NULL);
    
    /*
     *  Assign values to the array so that the optimizerer can't drastically
     *  alter or eliminate the loop.  Assigning a constant value will result
     *  in strange run time profiles as most optimizers will recognize an
     *  opportunity to achieve the same result without brute force.
     *  This code may need to be updated as optimizers get smarter.
     *  Optimizer should be used to reflect the way people really use
     *  a compiler.
     */
    switch(wsize)
    {
	case    1:
	    /* Byte */
	    for (c = 0; c < reps; ++c)
		for (bp = (uint8_t *)array; bp < end; ++bp)
		    *bp = (size_t)bp | 0xff;
	    break;
	case    2:
	    for (c = 0; c < reps; ++c)
		for (sp = (uint16_t *)array; sp < (uint16_t *)end; ++sp)
		    *sp = (size_t)sp | 0xffff;
	    break;
	case    4:
	    for (c = 0; c < reps; ++c)
		for (lp = (uint32_t *)array; lp < (uint32_t *)end; ++lp)
		    *lp = (size_t)lp | 0xffffffff;
	    break;
	case    8:
	    for (c = 0; c < reps; ++c)
		for (qp = (uint64_t *)array; qp < (uint64_t *)end; ++qp)
		    *qp = (uint64_t)qp;
	    break;
	default:
	    fprintf(stderr, "Invalid wsize: %d\n", wsize);
	    exit(1);
    }
    
    gettimeofday(&end_time, NULL);
    milliseconds = difftimeofday(&end_time, &start_time) / 1000;

    free(array);

    report_throughput(milliseconds, "array", array_size, reps, wsize);
    putchar('\n');
    return milliseconds;
}


unsigned long   write_test_low(uint64_t file_size)

{
    struct timeval  start_time, end_time;
    unsigned long   c,
		    milliseconds;
    int             fd;
    static char     buff[BLOCK_SIZE+1];

    srand(time(NULL));
    for (c = 0; c < BLOCK_SIZE; ++c)
	buff[c] = random();

    /* Test sequential write */
    printf("Writing %qu blocks of size %lu in sequential order...\n",
	   (long long unsigned int)BLOCKS, BLOCK_SIZE);
    printf("Total size = %qu mebibytes.\n",
	   (long long unsigned int)(BLOCK_SIZE * BLOCKS / MEBI));

    empty_bar(BAR_WIDTH);
    gettimeofday(&start_time, NULL);
    fd = open(TEMP_FILE, O_WRONLY | O_CREAT, 0600);
    if (fd == -1)
    {
	fprintf(stderr, "Error creating file.\n");
	return 1;
    }
    for (c = 0; c < BLOCKS; ++c)
    {
	if ( write(fd, buff, (size_t) BLOCK_SIZE) != BLOCK_SIZE )
	{
	    fprintf(stderr, "Write error.  Aborting...\n");
	    exit(1);
	}
	progress_bar(c, BLOCKS, BAR_WIDTH);
    }
    close(fd);

    /* 
     *  Although doing a sync before reading the
     *  end time would provide a more accurate measure of the total disk
     *  write time, it is not realistic for most applications, which
     *  do not have to wait for a sync before continuing computations or
     *  responding to the user.
     */
    gettimeofday(&end_time, NULL);
    sync();
    milliseconds = difftimeofday(&end_time, &start_time) / 1000;
    putchar('\n');
    report_throughput(milliseconds, "write", file_size, 1, BLOCK_SIZE);
    putchar('\n');
    return milliseconds;
}


/***************************************************************************
 *  Description:
 *      Shuffle array
 *      https://benpfaff.org/writings/clc/shuffle.html
 *
 *  History: 
 *  Date        Name        Modification
 *  2021-06-15  Jason Bacon Derive from Ben Pfaff writings
 ***************************************************************************/

void    shuffle(unsigned long array[], size_t n)
{
    size_t  i, j;
    unsigned long   temp;
    
    if (n > 1) 
    {
	for (i = 0; i < n - 1; i++) 
	{
	    j = i + random() / (RAND_MAX / (n - i) + 1);
	    temp = array[j];
	    array[j] = array[i];
	    array[i] = temp;
	}
    }
}


unsigned long   seek_test(uint64_t file_size)

{
    struct timeval  start_time, end_time;
    unsigned long   c,
		    milliseconds,
		    pos[BLOCKS];
    int             fd;
    static char     buff[BLOCK_SIZE+1];

    /* Test random seek */
    printf("Reading %qu blocks of size %lu in random order...\n",
	   (long long unsigned int)BLOCKS, BLOCK_SIZE);
    printf("Total size = %qu mebibytes.\n",
	   (long long unsigned int)(BLOCK_SIZE * BLOCKS / MEBI));
    
    /* Generate list of block start positions, then shuffle */
    for (c = 0; c < SEEKS; ++c)
	pos[c] = c * BLOCK_SIZE;
    shuffle(pos, BLOCKS);
    
    empty_bar(BAR_WIDTH);
    gettimeofday(&start_time, NULL);
    fd = open(TEMP_FILE, O_RDONLY);
    for (c = 0; c < SEEKS; ++c)
    {
	//printf("%lu\n", pos[c]);
	lseek(fd, pos[c], SEEK_SET);
	if ( read(fd, buff, (size_t) BLOCK_SIZE) < 0 )
	{
	    fprintf(stderr, "Read error.  Aborting...\n");
	    exit(1);
	}
	progress_bar(c, SEEKS, BAR_WIDTH);
    }
    close(fd);
    gettimeofday(&end_time, NULL);
    milliseconds = difftimeofday(&end_time, &start_time) / US_PER_MS;
    putchar('\n');
    report_throughput(milliseconds, "seek", file_size, 1, BLOCK_SIZE);
    putchar('\n');
    return milliseconds;
}


unsigned long   read_test_low(uint64_t file_size)

{
    struct timeval  start_time,
		    end_time;
    unsigned long   c,
		    milliseconds;
    int             fd;
    static char     buff[BLOCK_SIZE+1];
    
    /* Read sequentially */
    printf("Reading %qu blocks of size %lu in sequential order...\n",
	   (long long unsigned int)BLOCKS, BLOCK_SIZE);
    printf("Total size = %qu mebibytes.\n",
	   (long long unsigned int)(BLOCK_SIZE * BLOCKS / MEBI));

    empty_bar(BAR_WIDTH);
    gettimeofday(&start_time, NULL);
    fd = open(TEMP_FILE, O_RDONLY, 0);
    for (c = 0; c < BLOCKS; ++c)
    {
	if ( read(fd, buff, (size_t) BLOCK_SIZE) != BLOCK_SIZE )
	{
	    fprintf(stderr, "Read error.  Aborting...\n");
	    exit(1);
	}
	progress_bar(c, BLOCKS, BAR_WIDTH);
    }
    close(fd);
    gettimeofday(&end_time, NULL);
    milliseconds = difftimeofday(&end_time, &start_time) / 1000;
    putchar('\n');
    report_throughput(milliseconds, "read", file_size, 1, BLOCK_SIZE);
    putchar('\n');
    return milliseconds;
}


char    *size_str(uint64_t size, char *str, char *suffix)

{
    if ( suffix != NULL )
	snprintf(str, MSG_MAX, "%qu", (long long unsigned int)size);
    else if ( size >= GIBI )
	snprintf(str, MSG_MAX, "%0.2f GiB", (double)size / GIBI);
    else if ( size >= MEBI )
	snprintf(str, MSG_MAX, "%0.2f MiB", (double)size / MEBI);
    else if ( size >= KIBI )
	snprintf(str, MSG_MAX, "%0.2f KiB", (double)size / KIBI);
    else
	snprintf(str, MSG_MAX, "%0.2f B", (double)size);
    return str;
}


void    report_throughput(unsigned long milliseconds,
			    char *tag,
			    uint64_t  data_size,
			    int reps,
			    unsigned long block_size)

{
    char    data_size_str[MSG_MAX+1], 
	    block_size_str[MSG_MAX+1];
    
    printf(REPORT_FORMAT,
	size_str(data_size, data_size_str, NULL), tag, 
	size_str(block_size, block_size_str, NULL), (double)milliseconds,
	(data_size * reps / (double)MEBI) / ((double)milliseconds / MS_PER_SEC));
}


void    empty_bar(int width)

{
    int             c;

    putchar('[');
    for (c = 0; c < width; ++c)
	putchar(' ');
    putchar(']');
    for (c = 0; c < width + 1; ++c)
	putchar('\b');
}


void    progress_bar(unsigned long c, unsigned long max_c, unsigned long width)

{
    static int  stars;
    
    if ( c == 0 )
	stars = 0;
    
    /* Compensate for truncation error in max_c / width by counting stars */
    if ( ((c % (max_c / width)) == 0) && (stars++ < width) )
    {
	putchar('*');
	fflush(stdout);
    }
}


time_t  difftimeofday(struct timeval * start_time, struct timeval * end_time)

{
    return 1000000L * (start_time->tv_sec - end_time->tv_sec) + (start_time->tv_usec - end_time->tv_usec);
}


void    usage(char *argv[])

{
    fprintf(stderr, "Usage: %s [--help] [--trials N] [--file-size GiB]\n",
	    argv[0]);
    exit(EX_USAGE);
}


#ifdef _BORLAND
long        gettimeofday(struct timeval * tv, void *irrelevent)

{
    long    t1;

    _bios_timeofday(_TIME_GETCLOCK, &t1);
    tv->tv_sec = t1 / 18.2;
    tv->tv_usec = (t1 - (tv->tv_sec * 18.2)) * 1000000.0 / 18.2;
    return 0;
}
#endif

#ifdef _WIN32

/*
 *  gettimeofday() wrapper for Visual C++
 *  From openasthra.com
 */

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
    #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
    #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif
 
struct timezone 
{
    int  tz_minuteswest; /* minutes W of Greenwich */
    int  tz_dsttime;     /* type of dst correction */
};
 
long    gettimeofday(struct timeval *tv, struct timezone *tz)

{
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag;
   
    if (NULL != tv)
    {
	GetSystemTimeAsFileTime(&ft);
     
	tmpres |= ft.dwHighDateTime;
	tmpres <<= 32;
	tmpres |= ft.dwLowDateTime;
     
	/*converting file time to unix epoch*/
	tmpres /= 10;  /*convert into microseconds*/
	tmpres -= DELTA_EPOCH_IN_MICROSECS; 
	tv->tv_sec = (long)(tmpres / 1000000UL);
	tv->tv_usec = (long)(tmpres % 1000000UL);
    }
   
    if (NULL != tz)
    {
	if (!tzflag)
	{
	    _tzset();
	    tzflag++;
	}
	tz->tz_minuteswest = _timezone / 60;
	tz->tz_dsttime = _daylight;
    } 
    return 0;
}

#endif

