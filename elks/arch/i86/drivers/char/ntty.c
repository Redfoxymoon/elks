/*
 * arch/i86/drivers/char/tty.c - A simple tty driver
 * (C) 1997 Chad Page et. al
 */

/* 
 * This new tty sub-system builds around the character queue to provide a
 * VFS interface to the character drivers (what a mouthful! :)  
 */

#include <linuxmt/types.h>
#include <linuxmt/config.h>
#include <linuxmt/sched.h>
#include <linuxmt/fs.h>
#include <linuxmt/fcntl.h>
#include <linuxmt/errno.h>
#include <linuxmt/mm.h>
#include <linuxmt/termios.h>
#include <linuxmt/chqueue.h>
#include <linuxmt/ntty.h>
#include <linuxmt/major.h>

struct termios def_vals = { 	BRKINT,
				ONLCR,
				0,
				ECHO | ICANON,
				0,
				{3,28,127,21,4,0,1,0,17,19,26,0,18,15,23,22}
			};


/* FIXME */ /* should be an inline function */
void select_wait(wait_address, p) 
struct wait_queue ** wait_address;
register select_table * p;
{
	register struct select_table_entry * entry;
        if (!(p) || !wait_address)
                return;
        if ((p)->nr >= __MAX_SELECT_TABLE_ENTRIES)
                return;
        entry = (p)->entry + (p)->nr;
        entry->wait_address = wait_address;
        entry->wait.task = current;
        entry->wait.next = NULL;
        add_wait_queue(wait_address,&entry->wait);
        (p)->nr++;
}

#define TAB_SPACES 8

#define MAX_TTYS 8
struct tty ttys[MAX_TTYS];
extern struct tty_ops dircon_ops;
extern struct tty_ops bioscon_ops;
extern struct tty_ops rs_ops;

/* Turns a dev_t variable into its tty, or NULL if it's not valid */

struct tty * determine_tty(dev)
dev_t dev;
{
	int i, minor = MINOR(dev);
	register struct tty * ttyp;

	for (i = 0; i < MAX_TTYS; i++) {
		ttyp = &ttys[i];
		if (ttyp->minor == minor)
			return ttyp;
	}
	return 0; 
}

/* Just open it for now :) */
int tty_open(inode,file)
register struct inode *inode;
struct file *file;
{
	register struct tty * otty;

	if (otty = determine_tty(inode->i_rdev)) {
		return otty->ops->open(inode, file);
	}
	else
		return -ENODEV;

}

void tty_release(inode,file)
register struct inode *inode;
struct file *file;
{
	register struct tty *rtty;
	rtty = determine_tty(inode->i_rdev);
	if (rtty) 
		return rtty->ops->release(inode, file);
}

/* Write 1 byte to a terminal, with processing */

void tty_charout(tty, ch)
register struct tty *tty;
unsigned char ch;
{
	int j;

	switch (ch) {
		case '\t':		
			for (j = 0; j < TAB_SPACES; j++) tty_charout(tty, ' ');
			break;
#if 1
		case '\n':
			tty_charout(tty, '\r');
#endif		
		default:
			while (chq_addch(&tty->outq, ch) == -1) {
				tty->ops->write(tty);
			}
	};
}

void tty_echo(tty, ch)
register struct tty *tty;
unsigned char ch;
{
	if (tty->termios.c_lflag & ECHO) {
		tty_charout(tty, ch);
	}
}

/*
 *	Write to a terminal
 *
 */
 
int tty_write(inode,file,data,len)
struct inode *inode;
struct file *file;
char *data;
int len;
{
	register struct tty *tty=determine_tty(inode->i_rdev);
	int i = 0;

	while (i < len) {
		tty_charout(tty, peekb(current->t_regs.ds, data + i++)); 
	}
	tty->ops->write(tty);
	return i;
}

int tty_read(inode,file,data,len)
struct inode *inode;
struct file *file;
char *data;
int len;
{
	register struct tty *tty=determine_tty(inode->i_rdev);
	int i = 0, j = 0, k, l = 1;
	unsigned char ch, lch;
	int mode = (tty->termios.c_lflag & ICANON);

	while ((i < len) && (!mode || (j != '\n'))) { 
		if (tty->ops->read) {
			tty->ops->read(tty);
			l = 0;
		}
		j = chq_getch(&tty->inq, &ch, l);
		if (j == -1) {
			return -EINTR;
		}
		if (mode && (j == 4))
			break;
		if ((j != -1) && (!mode  || (j != '\b'))) {
			pokeb(current->t_regs.ds, (data + i++), ch);		
			tty_echo(tty, ch);
		}
		if (( mode && (j == '\b') && (i > 0))) {
			lch = ((peekb(current->t_regs.ds, (data + --i)) == '\t') ? TAB_SPACES : 1 );
			for (k = 0; k < lch ; k++)
				tty_echo(tty, ch);
		}
		tty->ops->write(tty);
	};
	return i;
}

int tty_ioctl(inode,file, cmd, arg)
struct inode *inode;
struct file *file;
int cmd;
char *arg;
{
	register struct tty *tty=determine_tty(inode->i_rdev);
	int retval;

	switch (cmd) {
		case TCGETS:
			return verified_memcpy_tofs(arg, &tty->termios, sizeof (struct termios));
			
			break;
		case TCSETS:
		case TCSETSW:
		case TCSETSF:
			return verified_memcpy_fromfs(&tty->termios, arg, sizeof(struct termios));
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

int tty_lseek(inode,file,offset,origin)
struct inode *inode;
struct file *file;
off_t offset;
int origin;
{
	return -ESPIPE;
}

#if 0 /* Default returns -ENOTDIR if no readdir exists, so this is redundant */
int tty_readdir()
{
	return -ENOTDIR;
}
#endif

int tty_select (inode, file, sel_type, wait)
struct inode * inode; /* how revolting, K&R style defs */
struct file * file;
int sel_type;
select_table * wait;
{
  register struct tty *tty=determine_tty(inode->i_rdev);

printk("tty_select called\n");
  switch (sel_type)
    {
    case SEL_IN:
      if (chq_peekch(tty->inq))
	{ printk("peekch: returning 1, peekch returns %d,inq.len=%d\n",
		 chq_peekch(tty->inq), tty->inq.len); return 1; }
    case SEL_EX: /* fall thru! */
      select_wait (&tty->sleep, wait);
      return 0;
    case SEL_OUT: /* Hm.  We can always write to a tty?  (not really) */
      return 1;
    }
}

static struct file_operations tty_fops =
{
	tty_lseek,
	tty_read,
	tty_write,
	NULL,
	tty_select,	/* Select - needs doing */
	tty_ioctl,		/* ioctl */
	tty_open,
	tty_release,
#ifdef BLOAT_FS
	NULL,
	NULL,
	NULL
#endif
};

void tty_init()
{
	int i;
	register struct tty * ttyp;

	for (i = 0; i < NUM_TTYS; i++) { 
		ttyp = &ttys[i];
		chq_init(&ttyp->inq, ttyp->inq_buf, INQ_SIZE);
		chq_init(&ttyp->outq, ttyp->outq_buf, OUTQ_SIZE);
	}

#ifdef CONFIG_CONSOLE_BIOS
	ttyp = &ttys[0];
	ttyp->ops = &bioscon_ops;
	ttyp->minor = 0;
	memcpy(&ttyp->termios, &def_vals, sizeof(struct termios));
#endif
#ifdef CONFIG_CONSOLE_DIRECT
	for (i = 0; i < 3; i++) {
		ttyp = &ttys[i];
		ttyp->ops = &dircon_ops;
		ttyp->minor = i;
		memcpy(&ttyp->termios, &def_vals, sizeof(struct termios));
	}
#endif
#ifdef CONFIG_CHAR_DEV_RS
	for (i = 4; i < 8; i++) {
		ttyp = &ttys[i];
		ttyp->ops = &rs_ops;
		ttyp->minor = i;
		memcpy(&ttyp->termios, &def_vals, sizeof(struct termios));
	}
#endif

	register_chrdev(TTY_MAJOR,"tty",&tty_fops);
}

