/* $Id: xtail.c,v 2.5 2000/06/04 09:09:03 chip Exp $ */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#define  INTERN
#include "xtail.h"

int sigcaught = 0;
RETSIGTYPE sigcatcher(int sig);


int main(int argc, char *argv[])
{
    int open_files_only, already_open, iteration, i;
    struct entry_descrip *entryp;
    struct stat sbuf;

    /* 
     * Initialize.
     */
    Progname = basename(argv[0]);
    List_file = new_entry_list(ENTRY_LIST_CHUNK);
    List_dir = new_entry_list(ENTRY_LIST_CHUNK);
    List_zap = new_entry_list(ENTRY_LIST_CHUNK);
    Sorted = FALSE;
    Reset_status = FALSE;
    Debug = FALSE;
    sigcatcher(0);


    /*
     * Place all of the entries onto lists.
     */
    for (i = 1 ; i < argc ; ++i)  {

	if (i == 1 && strcmp(argv[i], "-D") == 0) {
	    Debug = TRUE;
	    continue;
	}

	/*
	 * Temporarily throw this entry onto the end of the zapped list.
	 */
	entryp = new_entry(List_zap, argv[i]);

	/*
	 * Stat the file and get it to its proper place.
	 */
	switch (stat_entry(List_zap, last_entry(List_zap), &sbuf)) {

	case ENTRY_FILE:		/* move entry to file list	*/
	    move_entry(List_file, List_zap, last_entry(List_zap));
	    entryp->size = sbuf.st_size;
	    entryp->mtime = sbuf.st_mtime;
	    break;

	case ENTRY_DIR:			/* move entry to dir list	*/
	    move_entry(List_dir, List_zap, last_entry(List_zap));
	    entryp->size = sbuf.st_size;
	    entryp->mtime = sbuf.st_mtime;
	    if (scan_directory(entryp->name) != 0) {
		message(MSSG_OPEN, entryp);
		rmv_entry(List_dir, last_entry(List_dir));
	    }
	    break;

	case ENTRY_ZAP:			/* keep entry on zap list	*/
	    break;

	case ENTRY_SPECIAL:		/* entry is a special file	*/
	    message(MSSG_NOTAFIL, entryp);
	    rmv_entry(List_zap, last_entry(List_zap));
	    break;

	default:			/* stat error			*/
	    message(MSSG_STAT, entryp);
	    rmv_entry(List_zap, last_entry(List_zap));
	    break;

	}

    }

    /*
     * Make sure we are watching something reasonable.
     */
    if (List_file->num_entries == 0) {
	if (List_dir->num_entries == 0 && List_zap->num_entries == 0) {
	    (void) fprintf(stderr, "%s: no valid entries specified\n", Progname);
	    (void) exit(1);
	}
	(void) puts("\n*** warning - no files are being watched ***");
    }


    /*
     * From this point on we want to reset the status of an entry any
     * time we move it around to another list.
     */
    Reset_status = TRUE;


    /*
     * Force a check of everything first time through the loop.
     */
    iteration = CHECK_COUNT;


    /* 
     * Loop forever.
     */
    for (;;) {

	/*
	 * Once every CHECK_COUNT iterations check everything.
	 * All other times only look at the opened files.
	 */
	open_files_only = (++iteration < CHECK_COUNT);
	if (!open_files_only)
	    iteration = 0;


	/*
	 * Make sure that the most recently modified files are open.
	 */
	if (!Sorted)
	    fixup_open_files();


	/*
	 * Display what we are watching if a SIGINT was caught.
	 */
	if (sigcaught) {
	    show_status();
	    sigcatcher(0);
	}


	/*
	 * Go through all of the files looking for changes.
	 */
	Dprintf(stderr, ">>> checking files list (%s)\n",
	    (open_files_only ? "open files only" : "all files"));
	for (i = 0 ; i < List_file->num_entries ; ++i) {

	    entryp = List_file->list[i];
	    already_open = (entryp->fd > 0) ;

	    /*
	     * Ignore closed files except every CHECK_COUNT iterations.
	     */
	    if (!already_open && open_files_only)
		continue;

	    /*
	     * Get the status of this file.
	     */
	    switch (stat_entry(List_file, i, &sbuf)) {
	    case ENTRY_FILE:		/* got status OK		*/
		break;
	    case ENTRY_DIR:		/* huh??? it's now a dir	*/
		move_entry(List_dir, List_file, i--);
		continue;
	    case ENTRY_ZAP:		/* entry has been deleted	*/
		message(MSSG_ZAPPED, entryp);
		move_entry(List_zap, List_file, i--);
		continue;
	    case ENTRY_SPECIAL:		/* entry is a special file	*/
		message(MSSG_NOTAFIL, entryp);
		rmv_entry(List_file, i--);
		continue;
	    default:			/* stat error			*/
		message(MSSG_STAT, entryp);
		rmv_entry(List_file, i--);
		continue;
	    }


	    /*
	     * See if an opened file has been deleted.
	     */
	    if (already_open && sbuf.st_nlink == 0) {
		message(MSSG_ZAPPED, entryp);
		move_entry(List_zap, List_file, i--);
		continue;
	    }

	    /*
	     * If nothing has changed then continue on.
	     */
	    if (entryp->size==sbuf.st_size && entryp->mtime==sbuf.st_mtime)
		continue;

	    /*
	     * If the file isn't already open, then do so.
	     *   Note -- it is important that we call "fixup_open_files()"
	     *   at the end of the loop to make sure too many files don't
	     *   stay opened.
	     */
	    if (!already_open && open_entry(List_file, i) != 0) {
		--i;
		continue;
	    }

	    /*
	     * See if the file has been truncated.
	     */
	    if (sbuf.st_size < entryp->size) {
		message(MSSG_TRUNC, entryp);
		entryp->size = 0;
	    }

	    /*
	     * Seek to where the changes begin.
	     */
	    if (lseek(entryp->fd, entryp->size, 0) < 0) {
		message(MSSG_SEEK, entryp);
		rmv_entry(List_file, i--);
		continue;
	    }

	    /*
	     * Dump the recently added info.
	     */
	    {
		int nb;
    		static char buf[BUFSIZ];
		message(MSSG_BANNER, entryp);
		while ((nb = read(entryp->fd, buf, sizeof(buf))) > 0) {
		    (void) fwrite(buf, sizeof(char), (unsigned) nb, stdout);
		    entryp->size += nb;
		}
		if (nb < 0) {
		    message(MSSG_READ, entryp);
		    rmv_entry(List_file, i--);
		    continue;
		}
	    }

	    /*
	     * Update the modification time.
	     */
	    entryp->mtime = sbuf.st_mtime;

	    /*
	     * Since we've changed the mtime, the list might no longer be
	     * sorted.  However if this entry is already at the top of the
	     * list then it's OK.
	     */
	    if (i != 0)
		Sorted = FALSE;

	    /*
	     * If we've just opened the file then force a resort now to
	     * prevent too many files from being opened.
	     */
	    if (!already_open)
		fixup_open_files();

	}


	/*
	 * Go through list of nonexistent entries to see if any have appeared.
	 *   This is done only once every CHECK_COUNT iterations.
	 */
	if (!open_files_only) {
	    Dprintf(stderr, ">>> checking zapped list\n");
	    for (i = 0 ; i < List_zap->num_entries ; ++i) {
		entryp = List_zap->list[i];
		switch (stat_entry(List_zap, i, &sbuf)) {
		case ENTRY_FILE:	/* entry has appeared as a file	*/
		    message(MSSG_CREATED, entryp);
		    move_entry(List_file, List_zap, i--);
		    break;
		case ENTRY_DIR:		/* entry has appeared as a dir	*/
		    message(MSSG_CREATED, entryp);
		    move_entry(List_dir, List_zap, i--);
		    break;
		case ENTRY_ZAP:		/* entry still doesn't exist	*/
		    break;
		case ENTRY_SPECIAL:	/* entry is a special file	*/
		    message(MSSG_NOTAFIL, entryp);
	    	    rmv_entry(List_zap, i--);
		    break;
		default:		/* error - entry removed	*/
	    	    message(MSSG_STAT, entryp);
	    	    rmv_entry(List_zap, i--);
		    break;
		}
	    }
	}


	/*
	 * Go through the list of dirs to see if any new files were created.
	 *   This is done only once every CHECK_COUNT iterations.
	 */
	if (!open_files_only) {
	    Dprintf(stderr, ">>> checking directory list\n");
	    for (i = 0 ; !open_files_only && i < List_dir->num_entries ; ++i) {
		entryp = List_dir->list[i];
		switch (stat_entry(List_dir, i, &sbuf)) {
		case ENTRY_DIR:		/* got status OK		*/
		    break;
		case ENTRY_FILE:	/* huh??? it's now a reg file	*/
		    move_entry(List_file, List_dir, i--);
		    continue;
		case ENTRY_ZAP:		/* entry has been deleted	*/
	    	    message(MSSG_ZAPPED, entryp);
	    	    move_entry(List_zap, List_dir, i--);
		    continue;
		case ENTRY_SPECIAL:	/* entry is a special file	*/
		    message(MSSG_NOTAFIL, entryp);
		    rmv_entry(List_dir, i--);
		    continue;
		default:		/* stat error			*/
		    message(MSSG_STAT, entryp);
		    rmv_entry(List_dir, i--);
		    continue;
		}
		if (entryp->mtime == sbuf.st_mtime)
		    continue;
		if (scan_directory(entryp->name) != 0) {
		    message(MSSG_OPEN, entryp);
		    rmv_entry(List_dir, i--);
		}
		entryp->mtime = sbuf.st_mtime;
	    }
	}


	/*
	 * End of checking loop.
	 */
	{
	    extern unsigned sleep();
	    (void) fflush(stdout);
	    (void) sleep(SLEEP_TIME);
	}

    }

    /*NOTREACHED*/

}


RETSIGTYPE sigcatcher(int sig)
{
    if (sig == SIGQUIT)
	(void) exit(0);
    sigcaught = sig;
#ifdef STATUS_ENAB
    (void) signal(SIGINT, sigcatcher);
    (void) signal(SIGQUIT, sigcatcher);
#endif
}

