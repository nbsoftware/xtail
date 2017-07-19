/* $Id: miscfuncs.c,v 2.5 2000/06/04 09:09:03 chip Exp $ */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#include <time.h>
#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#else
# include <termio.h>
# define termios termio
#endif

#ifdef HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#ifndef STDC_HEADERS
 extern VOID *malloc(), *realloc();
 extern char *strcpy(), *strrchr();
#endif

#ifndef STDIN_FILENO
# define STDIN_FILENO 0
#endif

#include "xtail.h"


/*
 * Scan a directory for files not currently on a list.
 */
int scan_directory(const char *dirname)
{
    register int i;
    register struct dirent *dp;
    register struct entry_descrip **elist, *entryp;
    char *basename;
    struct stat sbuf;
    DIR *dirp;
    static char pathname[MAXNAMLEN];

    Dprintf(stderr, ">>> scanning directory '%s'\n", dirname);
    if ((dirp = opendir(dirname)) == NULL)
	return -1;

    (void) strcat(strcpy(pathname, dirname), "/");
    basename = pathname + strlen(pathname);

#define SKIP_DIR(D) \
    (D[0] == '.' && (D[1] == '\0' || (D[1] == '.' && D[2] == '\0')))

    while ((dp = readdir(dirp)) != NULL) {

	if (SKIP_DIR(dp->d_name))
	    continue;
	(void) strcpy(basename, dp->d_name);
	if (stat(pathname, &sbuf) != 0)
	    continue;
	if ((sbuf.st_mode & S_IFMT) != S_IFREG)
	    continue;

	for (i = List_file->num_entries, elist=List_file->list ; i > 0 ; --i, ++elist) {
	    if (strcmp((*elist)->name, pathname) == 0)
		break;
	}
	if (i > 0)
	    continue;

	for (i = List_zap->num_entries, elist=List_zap->list ; i > 0 ; --i, ++elist) {
	    if (strcmp((*elist)->name, pathname) == 0)
		break;
	}
	if (i > 0)
	    continue;

	entryp = new_entry(List_file, pathname);
	if (Reset_status) {
	    message(MSSG_CREATED, entryp);
	} else {
	    entryp->mtime = sbuf.st_mtime;
	    entryp->size = sbuf.st_size;
	}

    }

    (void) closedir(dirp);
    return 0;

}


/*
 * Compare mtime of two entries.  Used by the "qsort()" in "fixup_open_files()".
 */
int ecmp(const void *p1, const void *p2)
{
    const struct entry_descrip **ep1 = (const struct entry_descrip **) p1;
    const struct entry_descrip **ep2 = (const struct entry_descrip **) p2;
    return ((*ep2)->mtime - (*ep1)->mtime);
}


/*
 * Manage the open files.
 *   A small number of entries in "List_file" are kept open to minimize
 *   the overhead in checking for changes.  The strategy is to make sure
 *   the MAX_OPEN most recently modified files are all open.
 */
void fixup_open_files(void)
{
    register int i;
    register struct entry_descrip **elist;

    Dprintf(stderr, ">>> resorting file list\n");
    (void) qsort(
	(char *) List_file->list,
	List_file->num_entries,
	sizeof(struct entry_descrip *),
	ecmp);
    Sorted = TRUE;

    /*
     * Start at the end of the list.
     */
    i = last_entry(List_file);
    elist = &List_file->list[i];

    /*
     * All the files at the end of the list should be closed.
     */
    for ( ; i >= MAX_OPEN ; --i, --elist) {
	if ((*elist)->fd > 0) {
	    (void) close((*elist)->fd);
	    (*elist)->fd = 0;
	}
    }

    /*
     * The first MAX_OPEN files in the list should be open.
     */
    for ( ; i >= 0 ; --i, --elist) {
	if ((*elist)->fd <= 0)
	    (void) open_entry(List_file, i);
    }

}


/*
 * Standard message interface.
 *   There are two reasons for this message interface.  First, it provides
 *   consistent diagnostics for all the messages.  Second, it manages the
 *   filename banner display whenever we switch to a different file.
 *   Warning - "errno" is used in some of the messages, so care must be
 *   taken not to step on it before message() can be called.
 */
void message(int sel, const struct entry_descrip *e)
{
    static char *ofile = NULL;

    /*
     * Don't display the file banner if the file hasn't changed since last time.
     */
    if (sel == MSSG_BANNER && ofile != NULL && strcmp(ofile, e->name) == 0)
	return;

    /*
     * Make sure the message selector is within range.
     */
    if (sel < 0 || sel > MSSG_UNKNOWN)
	sel = MSSG_UNKNOWN;

    /*
     * Display the message.
     */
    if (mssg_list[sel] != NULL)
	(void) printf(mssg_list[sel], e->name, strerror(errno));

    ofile = (sel == MSSG_BANNER ? e->name : NULL);
}


/*
 * Display currently opened files.
 */
void show_status(void)
{
    int i, n;
    struct tm *tp;
    static char *monname[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static time_t tval0;
    time_t tval;

    /* give user a hint if they are whacking on SIGINT */
    time(&tval);
    if (tval0 != 0 && difftime(tval, tval0) < 3)
	(void) printf("(note: use \"%s\" SIQUIT to exit program)\n", quit_ch());
    tval0 = tval;

    (void) printf("\n*** recently changed files ***\n");
    for (i = 0, n = 0 ; i < List_file->num_entries ; ++i) {
	if (List_file->list[i]->fd > 0) {
	    tp = localtime(&List_file->list[i]->mtime);
	    (void) printf("%4d  %2d-%3s-%02d %02d:%02d:%02d  %s\n",
		++n,
		tp->tm_mday, monname[tp->tm_mon], (tp->tm_year % 100),
		tp->tm_hour, tp->tm_min, tp->tm_sec,
		List_file->list[i]->name
	    );
	}
    }

    (void) printf( 
	"currently watching:  %d files  %d dirs  %d unknown entries\n",
	List_file->num_entries, List_dir->num_entries, List_zap->num_entries);

    message(MSSG_NONE, (struct entry_descrip *) NULL);

}


char *quit_ch(void)
{
	struct termios tty;
	static char buf[8];
	int c;

	if (buf[0] != '\0')
		return buf;

	if (tcgetattr(STDIN_FILENO, &tty) < 0) {
		strcpy(buf, "?err?");
		return buf;
	}

	c = tty.c_cc[VQUIT];
	if (isprint(c) && !isspace(c)) {
		buf[0] = c;
		buf[1] = '\0';
	} else if (iscntrl(c)) {
		sprintf(buf, "ctrl-%c", c+0x40);
	} else {
		sprintf(buf, "0x%02X", c);
	}
	return buf;
}


VOID *safe_malloc(size_t n)
{
	VOID *p;
	if ((p = malloc(n)) == NULL) {
		fprintf(stderr, "%s: malloc(%d) failed\n", Progname, n);
		exit(2);
	}
	return p;
}

VOID *safe_realloc(VOID *p, size_t n)
{
	VOID *p1;
	if ((p1 = realloc(p, n)) == NULL) {
		fprintf(stderr, "%s: realloc(%d) failed\n", Progname, n);
		exit(2);
	}
	return p1;
}


char *safe_strdup(const char *p)
{
	return strcpy(safe_malloc(strlen(p)+1), p);
}


char *basename(char *p)
{
	char *q = strrchr(p, '/');
	return (q ? q+1 : p);
}


#ifndef HAVE_DIFFTIME
double difftime(time_t t1, time_t t0)
{
	double d1, d0;
	d1 = t1;
	d0 = t0;
	return (d1 - d0);
}
#endif


#ifndef HAVE_STRERROR
extern int errno;
extern char *sys_errlist[];
char *strerror(int err)
{
	return sys_errlist[err];
}
#endif

