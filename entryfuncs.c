/* $Id: entryfuncs.c,v 2.6 2000/06/05 07:10:57 chip Exp $ */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#include "xtail.h"

struct entry_list *new_entry_list(int chunk)
{
    struct entry_list *listp;
    listp = safe_malloc(sizeof(struct entry_list));
    listp->list = safe_malloc(chunk * sizeof(struct entry_descrip *));
    listp->num_entries = 0;
    listp->max_entries = chunk;
    listp->chunk = chunk;
    return listp;
}


static struct entry_descrip *E_append(struct entry_list *listp,
	struct entry_descrip *entryp)
{
    if (listp->num_entries >= listp->max_entries) {
	listp->max_entries += listp->chunk;
    	listp->list = safe_realloc(listp->list,
	    listp->max_entries * sizeof(struct entry_descrip *));
    }
    listp->list[listp->num_entries++] = entryp;
    Sorted = FALSE;
    return entryp;
}


static void E_remove(struct entry_list *listp, int entryno)
{
    while (++entryno < listp->num_entries)
	listp->list[entryno-1] = listp->list[entryno];
    --listp->num_entries;
    Sorted = FALSE;
}


static char *list_name(struct entry_list *listp) /* for debug output only */
{
    if (listp == List_file)	return "<file>";
    if (listp == List_dir)	return "<dir>";
    if (listp == List_zap)	return "<zap>";
    return "?unknown?";
}


/*
 * Create a new entry description and append it to a list.
 */
struct entry_descrip *new_entry(struct entry_list *listp, const char *name)
{
    struct entry_descrip *entryp;

    Dprintf(stderr, ">>> creating entry '%s' on %s list\n",
	name, list_name(listp));

    entryp = safe_malloc(sizeof(struct entry_descrip));
    entryp->name = safe_strdup(name);
    entryp->fd = 0;
    entryp->size =  0;
    entryp->mtime = 0;

    return E_append(listp,entryp);
}


/*
 * Remove an entry from a list and free up its space.
 */
void rmv_entry(struct entry_list *listp, int entryno)
{
    struct entry_descrip *entryp = listp->list[entryno];

    Dprintf(stderr, ">>> removing entry '%s' from %s list\n",
	listp->list[entryno]->name, list_name(listp));
    E_remove(listp,entryno);
    if (entryp->fd > 0)
	(void) close(entryp->fd);
    free((VOID *) entryp->name);
    free((VOID *) entryp);
}


/*
 * Move an entry from one list to another.
 *	In addition we close up the entry if appropriate.
 */
void move_entry(struct entry_list *dst_listp, struct entry_list *src_listp,
	int src_entryno)
{
    struct entry_descrip *entryp = src_listp->list[src_entryno];

    Dprintf(stderr, ">>> moving entry '%s' from %s list to %s list\n",
	src_listp->list[src_entryno]->name,
	list_name(src_listp), list_name(dst_listp));
    if (entryp->fd > 0) {
	(void) close(entryp->fd);
	entryp->fd = 0;
    }
    E_remove(src_listp,src_entryno);
    (void) E_append(dst_listp,entryp);
    if (Reset_status) {
	entryp->size = 0;
	entryp->mtime = 0;
    }
}


/*
 * Get the inode status for an entry.
 *	Returns code describing the status of the entry.
 */
int stat_entry(struct entry_list *listp, int entryno, struct stat *sbuf)
{
    int status;
    struct entry_descrip *entryp = listp->list[entryno];

    status = 
	(entryp->fd > 0 ? fstat(entryp->fd,sbuf) : stat(entryp->name,sbuf));

    if (status != 0)
	return (errno == ENOENT ? ENTRY_ZAP : ENTRY_ERROR);

    switch (sbuf->st_mode & S_IFMT) {
	case S_IFREG:	return ENTRY_FILE;
	case S_IFDIR:	return ENTRY_DIR;
	default:	return ENTRY_SPECIAL;
    }

    /*NOTREACHED*/
}


/*
 * Open an entry.
 *	Returns 0 if the open is successful, else returns errno.  In the case
 *	of an error, an appropriate diagnostic will be printed, and the entry
 *	will be moved or deleted as required.  If the entry is already opened,
 *	then no action will occur and 0 will be returned.
 */
int open_entry(struct entry_list *listp, int entryno)
{
    struct entry_descrip *entryp = listp->list[entryno];

    if (entryp->fd > 0)
	return 0;

    Dprintf(stderr, ">>> opening entry '%s' on %s list\n",
	listp->list[entryno]->name, list_name(listp));
    if ((entryp->fd = open(entryp->name, O_RDONLY)) > 0)
	return 0;

    if (errno == ENOENT) {
	message(MSSG_ZAPPED, entryp);
	move_entry(List_zap, listp, entryno);
    } else {
	message(MSSG_OPEN, entryp);
	rmv_entry(listp, entryno);
    }
    return -1;
}



