/*	bootimage.c - Load an image and start it.	Author: Kees J. Bot
 *								19 Jan 1992
 */
#define BIOS		1	/* Can only be used under the BIOS. */
#define nil 0
#define _POSIX_SOURCE	1
#define _MINIX		1
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <a.out.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/syslib.h>
#include <minix/tty.h>
#include <sys/video.h>
#include <kernel/const.h>
#include <kernel/type.h>
#include <machine/partition.h>
#include "rawfs.h"
#include "image.h"
#include "emem.h"
#include "boot.h"

#include <machine/multiboot.h>
#include <machine/elf.h>


static int block_size = 0;
static int verboseboot = VERBOSEBOOT_QUIET;

#define DEBUG_PRINT(params, level) do { \
	if (verboseboot >= (level)) printf params; } while (0)
#define DEBUGBASIC(params) DEBUG_PRINT(params, VERBOSEBOOT_BASIC)
#define DEBUGEXTRA(params) DEBUG_PRINT(params, VERBOSEBOOT_EXTRA)
#define DEBUGMAX(params)   DEBUG_PRINT(params, VERBOSEBOOT_MAX)

extern int serial_line;
extern u16_t vid_port;         /* Video i/o port. */
extern u32_t vid_mem_base;     /* Video memory base address. */
extern u32_t vid_mem_size;     /* Video memory size. */
extern u32_t mbdev;            /* Device number in multiboot format */

#define click_shift	clck_shft	/* 7 char clash with click_size. */

/* Some kernels have extra features: */
#define K_I386	 0x0001	/* Make the 386 transition before you call me. */
#define K_CLAIM	 0x0002	/* I will acquire my own bss pages, thank you. */
#define K_CHMEM  0x0004	/* This kernel listens to chmem for its stack size. */
#define K_HIGH   0x0008	/* Load mm, fs, etc. in extended memory. */
#define K_HDR	 0x0010	/* No need to patch sizes, kernel uses the headers. */
#define K_RET	 0x0020	/* Returns to the monitor on reboot. */
#define K_INT86	 0x0040	/* Requires generic INT support. */
#define K_MEML	 0x0080	/* Pass a list of free memory. */
#define K_BRET	 0x0100	/* New monitor code on shutdown in boot parameters. */
#define K_KHIGH  0x0200	/* Load kernel in extended memory. */
#define K_ALL	 0x03FF	/* All feature bits this monitor supports. */


/* Data about the different processes. */

#define PROCESS_MAX	16	/* Must match the space in kernel/mpx.x */
#define KERNEL_IDX	0	/* The first process is the kernel. */
#define FS		2	/* The third must be fs. */

struct process {	/* Per-process memory adresses. */
	u32_t	entry;		/* Entry point. */
	u32_t	cs;		/* Code segment. */
	u32_t	ds;		/* Data segment. */
	u32_t	data;		/* To access the data segment. */
	u32_t	end;		/* End of this process, size = (end - cs). */
} process[PROCESS_MAX];
int n_procs;			/* Number of processes. */

/* Magic numbers in process' data space. */
#define MAGIC_OFF	0	/* Offset of magic # in data seg. */
#define CLICK_OFF	2	/* Offset in kernel text to click_shift. */
#define FLAGS_OFF	4	/* Offset in kernel text to flags. */
#define KERNEL_D_MAGIC	0x526F	/* Kernel magic number. */

/* Offsets of sizes to be patched into kernel and fs. */
#define P_SIZ_OFF	0	/* Process' sizes into kernel data. */
#define P_INIT_OFF	4	/* Init cs & sizes into fs data. */

/* Where multiboot info struct goes in memory */
#define MULTIBOOT_INFO_ADDR 0x9500

#define between(a, c, z)	((unsigned) ((c) - (a)) <= ((z) - (a)))

char *select_image(char *image);
size_t strspn(const char *string, const char *in);
char * strpbrk(register const char *string, register const char *brk);
char * strtok(register char *string, const char *separators);
char * strdup(const char *s1);

void pretty_image(const char *image)
/* Pretty print the name of the image to load.  Translate '/' and '_' to
 * space, first letter goes uppercase.  An 'r' before a digit prints as
 * 'revision'.  E.g. 'minix/1.6.16r10' -> 'Minix 1.6.16 revision 10'.
 * The idea is that the part before the 'r' is the official Minix release
 * and after the 'r' you can put version numbers for your own changes.
 */
{
	int up= 0, c;

	while ((c= *image++) != 0) {
		if (c == '/' || c == '_') c= ' ';

		if (c == 'r' && between('0', *image, '9')) {
			printf(" revision ");
			continue;
		}
		if (!up && between('a', c, 'z')) c= c - 'a' + 'A';

		if (between('A', c, 'Z')) up= 1;

		putch(c);
	}
}

#define RAW_ALIGN	16
#define BUFSIZE_ZEROS	128

void raw_clear(u32_t addr, u32_t count)
/* Clear "count" bytes at absolute address "addr". */
{
	static char zerosdata[BUFSIZE_ZEROS + RAW_ALIGN];
	char *zeros = zerosdata + RAW_ALIGN - (unsigned) &zerosdata % RAW_ALIGN;
	u32_t dst;
	u32_t zct;

	zct= BUFSIZE_ZEROS;
	if (zct > count) zct= count;
	raw_copy(addr, mon2abs(zeros), zct);
	count-= zct;

	while (count > 0) {
		dst= addr + zct;
		if (zct > count) zct= count;
		raw_copy(dst, addr, zct);
		count-= zct;
		zct*= 2;
	}
}

/* Align a to a multiple of n (a power of 2): */
#define align(a, n)	(((u32_t)(a) + ((u32_t)(n) - 1)) & ~((u32_t)(n) - 1))
unsigned click_shift;
unsigned click_size;	/* click_size = Smallest kernel memory object. */
unsigned k_flags;	/* Not all kernels are created equal. */
u32_t reboot_code;	/* Obsolete reboot code return pointer. */
int do_multiboot;

int params2params(char *params, size_t psize)
/* Repackage the environment settings for the kernel. */
{
	size_t i, n;
	environment *e;
	char *name, *value;
	dev_t dev;

	i= 0;
	for (e= env; e != nil; e= e->next) {
		name= e->name;
		value= e->value;

		if (!(e->flags & E_VAR)) continue;

		if (e->flags & E_DEV) {
			if ((dev= name2dev(value)) == -1) return 0;
			value= ul2a10((u16_t) dev);
		}

		n= i + strlen(name) + 1 + strlen(value) + 1;
		if (n < psize) {
			strcpy(params + i, name);
			strcat(params + i, "=");
			strcat(params + i, value);
		}
		i= n;
	}

	if (!(k_flags & K_MEML)) {
		/* Require old memory size variables. */

		value= ul2a10((mem[0].base + mem[0].size) / 1024);
		n= i + 7 + 1 + strlen(value) + 1;
		if (n < psize) {
			strcpy(params + i, "memsize=");
			strcat(params + i, value);
		}
		i= n;
		value= ul2a10(mem[1].size / 1024);
		n= i + 7 + 1 + strlen(value) + 1;
		if (n < psize) {
			strcpy(params + i, "emssize=");
			strcat(params + i, value);
		}
		i= n;
	}

	if (i >= psize) {
		printf("Too many boot parameters\n");
		return 0;
	}
	params[i]= 0;	/* End marked with empty string. */
	return 1;
}

void patch_sizes(void)
/* Patch sizes of each process into kernel data space, kernel ds into kernel
 * text space, and sizes of init into data space of fs.  All the patched
 * numbers are based on the kernel click size, not hardware segments.
 */
{
	u16_t text_size, data_size;
	int i;
	struct process *procp, *initp;
	u32_t doff;

	if (k_flags & K_HDR) return;	/* Uses the headers. */

	/* Patch text and data sizes of the processes into kernel data space.
	 */
	doff= process[KERNEL_IDX].data + P_SIZ_OFF;

	for (i= 0; i < n_procs; i++) {
		procp= &process[i];
		text_size= (procp->ds - procp->cs) >> click_shift;
		data_size= (procp->end - procp->ds) >> click_shift;

		/* Two words per process, the text and data size: */
		put_word(doff, text_size); doff+= 2;
		put_word(doff, data_size); doff+= 2;

		initp= procp;	/* The last process must be init. */
	}

	if (k_flags & (K_HIGH|K_MEML)) return;	/* Doesn't need FS patching. */

	/* Patch cs and sizes of init into fs data. */
	put_word(process[FS].data + P_INIT_OFF+0, initp->cs >> click_shift);
	put_word(process[FS].data + P_INIT_OFF+2, text_size);
	put_word(process[FS].data + P_INIT_OFF+4, data_size);
}

int selected(const char *name)
/* True iff name has no label or the proper label. */
{
	char *colon, *label;
	int cmp;

	if ((colon= strchr(name, ':')) == nil) return 1;
	if ((label= b_value("label")) == nil) return 1;

	*colon= 0;
	cmp= strcmp(label, name);
	*colon= ':';
	return cmp == 0;
}

static u32_t proc_size(const struct image_header *hdr)
/* Return the size of a process in sectors as found in an image. */
{
	u32_t len= hdr->process.a_text;

	if (hdr->process.a_flags & A_PAL) len+= hdr->process.a_hdrlen;
	if (hdr->process.a_flags & A_SEP) len= align(len, SECTOR_SIZE);
	len= align(len + hdr->process.a_data, SECTOR_SIZE);

	return len >> SECTOR_SHIFT;
}

off_t image_off, image_sectors, image_bytes;
u32_t (*vir2sec)(u32_t vsec);	/* Where is a sector on disk? */

u32_t file_vir2sec(u32_t vsec)
/* Translate a virtual sector number to an absolute disk sector. */
{
	off_t blk;

	if(!block_size) { errno = 0;  return -1; }

	if ((blk= r_vir2abs(vsec / RATIO(block_size))) == -1) {
		errno= EIO;
		return -1;
	}
	return blk == 0 ? 0 : lowsec + blk * RATIO(block_size) + vsec % RATIO(block_size);
}

u32_t flat_vir2sec(u32_t vsec)
/* Simply add an absolute sector offset to vsec. */
{
	return lowsec + image_off + vsec;
}

char *get_sector(u32_t vsec)
/* Read a sector "vsec" from the image into memory and return its address.
 * Return nil on error.  (This routine tries to read an entire track, so
 * the next request is usually satisfied from the track buffer.)
 */
{
	u32_t sec;
	int r;
#define SECBUFS 16
	static char bufdata[SECBUFS * SECTOR_SIZE + RAW_ALIGN];
	static size_t count;		/* Number of sectors in the buffer. */
	static u32_t bufsec;		/* First Sector now in the buffer. */
	char *buf = bufdata + RAW_ALIGN - (unsigned) &bufdata % RAW_ALIGN;

	if (vsec == 0) count= 0;	/* First sector; initialize. */

	if ((sec= (*vir2sec)(vsec)) == -1) return nil;

	if (sec == 0) {
		/* A hole. */
		count= 0;
		memset(buf, 0, SECTOR_SIZE);
		return buf;
	}

	/* Can we return a sector from the buffer? */
	if ((sec - bufsec) < count) {
		return buf + ((size_t) (sec - bufsec) << SECTOR_SHIFT);
	}

	/* Not in the buffer. */
	count= 0;
	bufsec= sec;

	/* Read a whole track if possible. */
	while (++count < SECBUFS && !dev_boundary(bufsec + count)) {
		vsec++;
		if ((sec= (*vir2sec)(vsec)) == -1) break;

		/* Consecutive? */
		if (sec != bufsec + count) break;
	}

	/* Actually read the sectors. */
	if ((r= readsectors(mon2abs(buf), bufsec, count)) != 0) {
		readerr(bufsec, r);
		count= 0;
		errno= 0;
		return nil;
	}
	return buf;
}

int get_clickshift(u32_t ksec, struct image_header *hdr)
/* Get the click shift and special flags from kernel text. */
{
	char *textp;

	if ((textp= get_sector(ksec)) == nil) return 0;

	if (hdr->process.a_flags & A_PAL) textp+= hdr->process.a_hdrlen;
	click_shift= * (u16_t *) (textp + CLICK_OFF);
	k_flags= * (u16_t *) (textp + FLAGS_OFF);

	if ((k_flags & ~K_ALL) != 0) {
		printf("%s requires features this monitor doesn't offer\n",
			hdr->name);
		return 0;
	}

	if (click_shift < HCLICK_SHIFT || click_shift > 16) {
		printf("%s click size is bad\n", hdr->name);
		errno= 0;
		return 0;
	}

	click_size= 1 << click_shift;

	return 1;
}

int get_segment(u32_t *vsec, long *size, u32_t *addr, u32_t limit)
/* Read *size bytes starting at virtual sector *vsec to memory at *addr. */
{
	char *buf;
	size_t cnt, n;

	cnt= 0;
	while (*size > 0) {
		if (cnt == 0) {
			if ((buf= get_sector((*vsec)++)) == nil) return 0;
			cnt= SECTOR_SIZE;
		}
		if (*addr + click_size > limit) 
		{
			DEBUGEXTRA(("get_segment: out of memory; "
				"addr=0x%lx; limit=0x%lx; size=%lx\n", 
				*addr, limit, size));
			errno= ENOMEM; 
			return 0; 
		}
		n= click_size;
		if (n > cnt) n= cnt;
		DEBUGMAX(("raw_copy(0x%lx, 0x%lx/0x%x, 0x%lx)... ", 
			*addr, mon2abs(buf), buf, n));
		raw_copy(*addr, mon2abs(buf), n);
		DEBUGMAX(("done\n"));
		*addr+= n;
		*size-= n;
		buf+= n;
		cnt-= n;
	}

	/* Zero extend to a click. */
	n= align(*addr, click_size) - *addr;
	DEBUGMAX(("raw_clear(0x%lx, 0x%lx)... ", *addr, n));
	raw_clear(*addr, n);
	DEBUGMAX(("done\n"));
	*addr+= n;
	*size-= n;
	return 1;
}

static void restore_screen(void)
{
	struct boot_tty_info boot_tty_info;
	u32_t info_location;
#define LINES 25
#define CHARS 80
	static u16_t consolescreen[LINES][CHARS];

	/* Try and find out what the main console was displaying
	 * by looking into video memory.
	 */

	info_location = vid_mem_base+vid_mem_size-sizeof(boot_tty_info);
        raw_copy(mon2abs(&boot_tty_info), info_location,
                sizeof(boot_tty_info));

        if(boot_tty_info.magic == TTYMAGIC) {
                if((boot_tty_info.flags & (BTIF_CONSORIGIN|BTIF_CONSCURSOR)) ==
			(BTIF_CONSORIGIN|BTIF_CONSCURSOR)) {
			int line;
			raw_copy(mon2abs(consolescreen), 
				vid_mem_base + boot_tty_info.consorigin,
				sizeof(consolescreen));
			clear_screen();
			for(line = 0; line < LINES; line++) {
				int ch;
				for(ch = 0; ch < CHARS; ch++) {
					u16_t newch = consolescreen[line][ch] & BYTE;
					if(newch < ' ') newch = ' ';
					putch(newch);
				}
			}
		}
        }
}

int split_module_list(char *modules)
{
	int i;
	char *c, *s;

	for (s= modules, i= 1; (c= strrchr(s, ' ')) != NULL; i++) {
	    *c = '\0';
	}

	return i;
}

void exec_mb(char *kernel, char* modules)
/* Get a Minix image into core, patch it up and execute. */
{
	int i;
	static char hdr[SECTOR_SIZE];
	char *buf;
	u32_t vsec, addr, limit, n, totalmem = 0;
	u16_t kmagic, mode;
	char *console;
	char params[SECTOR_SIZE];
	extern char *sbrk(int);
	char *verb;
	u32_t text_vaddr, text_paddr, text_filebytes, text_membytes;
	u32_t data_vaddr, data_paddr, data_filebytes, data_membytes;
	u32_t pc;
	u32_t text_offset, data_offset;
	i32_t segsize;
	int r;
	u32_t cs, ds;
	char *modstring, *mod;
	multiboot_info_t *mbinfo;
	multiboot_module_t *mbmodinfo;
	u32_t mbinfo_size, mbmodinfo_size;
	char *memvar;
	memory *mp;
	u32_t mod_cmdline_start, kernel_cmdline_start;
	u32_t modstringlen;
	int modnr;

	/* The stack is pretty deep here, so check if heap and stack collide. */
	(void) sbrk(0);

	if ((verb= b_value(VERBOSEBOOTVARNAME)) != nil)
		verboseboot = a2l(verb);

	printf("\nLoading %s\n", kernel);

	vsec= 0;			/* Load this sector from kernel next. */
	addr= mem[0].base;		/* Into this memory block. */
	limit= mem[0].base + mem[0].size;
	if (limit > caddr) limit= caddr;

	/* set click size for get_segment */
	click_size = PAGE_SIZE;

	k_flags = K_KHIGH|K_BRET|K_MEML|K_INT86|K_RET|K_HDR
	    |K_HIGH|K_CHMEM|K_I386;

	/* big kernels must be loaded into extended memory */
	addr= mem[1].base;
	limit= mem[1].base + mem[1].size;

	/* Get first sector */
	DEBUGEXTRA(("get_sector\n"));
	if ((buf= get_sector(vsec++)) == nil) {
	    DEBUGEXTRA(("get_sector failed\n"));
	    return;
	}
	memcpy(hdr, buf, SECTOR_SIZE);

	/* Get ELF header */
	DEBUGEXTRA(("read_header_elf\n"));
	r = read_header_elf(hdr, &text_vaddr, &text_paddr,
			    &text_filebytes, &text_membytes,
			    &data_vaddr, &data_paddr,
			    &data_filebytes, &data_membytes,
			    &pc, &text_offset, &data_offset);
	if (r < 0) { errno= ENOEXEC; return; }

	/* Read the text segment. */
	addr = text_paddr;
	segsize = (i32_t) text_filebytes;
	vsec = text_offset / SECTOR_SIZE;
	DEBUGEXTRA(("get_segment(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		    vsec, segsize, addr, limit));
	if (!get_segment(&vsec, &segsize, &addr, limit)) return;
	DEBUGEXTRA(("get_segment done vsec=0x%lx size=0x%lx "
		    "addr=0x%lx\n",
		    vsec, segsize, addr));

	/* Read the data segment. */
	addr = data_paddr;
	segsize = (i32_t) data_filebytes;
	vsec = data_offset / SECTOR_SIZE;

	DEBUGEXTRA(("get_segment(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		    vsec, segsize, addr, limit));
	if (!get_segment(&vsec, &segsize, &addr, limit)) return;
	DEBUGEXTRA(("get_segment done vsec=0x%lx size=0x%lx "
		    "addr=0x%lx\n",
		    vsec, segsize, addr));

	n = data_membytes - align(data_filebytes, click_size);

	/* Zero out bss. */
	DEBUGEXTRA(("\nraw_clear(0x%lx, 0x%lx); limit=0x%lx... ", addr, n, limit));
	if (addr + n > limit) { errno= ENOMEM; return; }
	raw_clear(addr, n);
	DEBUGEXTRA(("done\n"));
	addr+= n;

	/* Check the kernel magic number. */
	raw_copy(mon2abs(&kmagic),
		 data_paddr + MAGIC_OFF, sizeof(kmagic));
	if (kmagic != KERNEL_D_MAGIC) {
		printf("Kernel magic number is incorrect (0x%x@0x%lx)\n",
			kmagic, data_paddr + MAGIC_OFF);
		errno= 0;
		return;
	}

	/* Translate the boot parameters to what Minix likes best. */
	DEBUGEXTRA(("params2params(0x%x, 0x%x)... ", params, sizeof(params)));
	if (!params2params(params, sizeof(params))) { errno= 0; return; }
	DEBUGEXTRA(("done\n"));

	/* Create multiboot info struct */
	mbinfo = malloc(sizeof(multiboot_info_t));
	if (mbinfo == nil) { errno= ENOMEM; return; }
	memset(mbinfo, 0, sizeof(multiboot_info_t));

	/* Module info structs start where kernel ends */
	mbinfo->mods_addr = addr;

	modstring = strdup(modules);
	if (modstring == nil) {errno = ENOMEM; return; }
	modstringlen = strlen(modules);
	mbinfo->mods_count = split_module_list(modules);

	mbmodinfo_size = sizeof(multiboot_module_t) * mbinfo->mods_count;
	mbmodinfo = malloc(mbmodinfo_size);
	if (mbmodinfo == nil) { errno= ENOMEM; return; }
	addr+= mbmodinfo_size;
	addr= align(addr, click_size);

	mod_cmdline_start = mbinfo->mods_addr + sizeof(multiboot_module_t) *
	    mbinfo->mods_count;

	raw_copy(mod_cmdline_start, mon2abs(modules),
		 modstringlen+1);

	mbmodinfo[0].cmdline = mod_cmdline_start;
	modnr = 1;
	for (i= 0; i < modstringlen; ++i) {
	    if (modules[i] == '\0') {
		mbmodinfo[modnr].cmdline = mod_cmdline_start + i + 1;
		++modnr;
	    }
	}

	kernel_cmdline_start = mod_cmdline_start + modstringlen + 1;
	mbinfo->cmdline = kernel_cmdline_start;
	raw_copy(kernel_cmdline_start, mon2abs(kernel),
		 strlen(kernel)+1);

	mbinfo->flags = MULTIBOOT_INFO_MODS|MULTIBOOT_INFO_CMDLINE|
	    MULTIBOOT_INFO_BOOTDEV|MULTIBOOT_INFO_MEMORY;

	mbinfo->boot_device = mbdev;
	mbinfo->mem_lower = mem[0].size/1024;
	mbinfo->mem_upper = mem[1].size/1024;

	for (i = 0, mod = strtok(modstring, " "); mod != nil;
	     mod = strtok(nil, " "), i++) {

		mod = select_image(mod);
		if (mod == nil) {errno = 0; return; }

		mbmodinfo[i].mod_start = addr;
		mbmodinfo[i].mod_end = addr + image_bytes;
		mbmodinfo[i].pad = 0;

		segsize= image_bytes;
		vsec= 0;
		DEBUGEXTRA(("get_segment(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
		       vsec, segsize, addr, limit));
		if (!get_segment(&vsec, &segsize, &addr, limit)) return;
		DEBUGEXTRA(("get_segment done vsec=0x%lx size=0x%lx "
		       "addr=0x%lx\n",
		       vsec, segsize, addr));
		addr+= segsize;
		addr= align(addr, click_size);
	}
	free(modstring);

	DEBUGEXTRA(("modinfo raw_copy: dst 0x%lx src 0x%lx sz 0x%lx\n",
	    mbinfo->mods_addr, mon2abs(mbmodinfo),
	    mbmodinfo_size));
	raw_copy(mbinfo->mods_addr, mon2abs(mbmodinfo),
	    mbmodinfo_size);
	free(mbmodinfo);

	raw_copy(MULTIBOOT_INFO_ADDR, mon2abs(mbinfo),
		 sizeof(multiboot_info_t));
	free(mbinfo);

	/* Run the trailer function just before starting Minix. */
	DEBUGEXTRA(("run_trailer()... "));
	if (!run_trailer()) { errno= 0; return; }
	DEBUGEXTRA(("done\n"));

	/* Set the video to the required mode. */
	if ((console= b_value("console")) == nil || (mode= a2x(console)) == 0) {
		mode= strcmp(b_value("chrome"), "color") == 0 ? COLOR_MODE :
								MONO_MODE;
	}
	DEBUGEXTRA(("set_mode(%d)... ", mode));
	set_mode(mode);
	DEBUGEXTRA(("done\n"));

	/* Close the disk. */
	DEBUGEXTRA(("dev_close()... "));
	(void) dev_close();
	DEBUGEXTRA(("done\n"));

	/* Minix. */
	cs = ds = text_paddr;
	DEBUGEXTRA(("minix(0x%lx, 0x%lx, 0x%lx, 0x%x, 0x%x, 0x%lx)\n",
		pc, cs, ds, params, sizeof(params), 0));
	minix(pc, cs, ds, params, sizeof(params), 0);

	if (!(k_flags & K_BRET)) {
		extern u32_t reboot_code;
		raw_copy(mon2abs(params), reboot_code, sizeof(params));
	}
	parse_code(params);

	/* Return from Minix.  Things may have changed, so assume nothing. */
	fsok= -1;
	errno= 0;

	/* Read leftover character, if any. */
	scan_keyboard();

	/* Restore screen contents. */
	restore_screen();
}

void exec_image(char *image)
/* Get a Minix image into core, patch it up and execute. */
{
	int i;
	struct image_header hdr;
	char *buf;
	u32_t vsec, addr, limit, aout, n, totalmem = 0;
	struct process *procp;		/* Process under construction. */
	long a_text, a_data, a_bss, a_stack;
	int banner= 0;
	long processor= a2l(b_value("processor"));
	u16_t kmagic, mode;
	char *console;
	char params[SECTOR_SIZE];
	extern char *sbrk(int);
	char *verb;

	/* The stack is pretty deep here, so check if heap and stack collide. */
	(void) sbrk(0);

	if ((verb= b_value(VERBOSEBOOTVARNAME)) != nil)
		verboseboot = a2l(verb);

	printf("\nLoading ");
	pretty_image(image);
	printf(".\n");

	vsec= 0;			/* Load this sector from image next. */
	addr= mem[0].base;		/* Into this memory block. */
	limit= mem[0].base + mem[0].size;
	if (limit > caddr) limit= caddr;

	/* Allocate and clear the area where the headers will be placed. */
	aout = (limit -= PROCESS_MAX * A_MINHDR);

	/* Clear the area where the headers will be placed. */
	raw_clear(aout, PROCESS_MAX * A_MINHDR);

	/* Read the many different processes: */
	for (i= 0; vsec < image_sectors; i++) {
		u32_t startaddr;
		startaddr = addr;
		if (i == PROCESS_MAX) {
			printf("There are more then %d programs in %s\n",
				PROCESS_MAX, image);
			errno= 0;
			return;
		}
		procp= &process[i];

		/* Read header. */
		DEBUGEXTRA(("Reading header... "));
		for (;;) {
			if ((buf= get_sector(vsec++)) == nil) return;

			memcpy(&hdr, buf, sizeof(hdr));

			if (BADMAG(hdr.process)) { errno= ENOEXEC; return; }

			/* Check the optional label on the process. */
			if (selected(hdr.name)) break;

			/* Bad label, skip this process. */
			vsec+= proc_size(&hdr);
		}
		DEBUGEXTRA(("done\n"));

		/* Sanity check: an 8086 can't run a 386 kernel. */
		if (hdr.process.a_cpu == A_I80386 && processor < 386) {
			printf("You can't run a 386 kernel on this 80%ld\n",
				processor);
			errno= 0;
			return;
		}

		/* Get the click shift from the kernel text segment. */
		if (i == KERNEL_IDX) {
			if (!get_clickshift(vsec, &hdr)) return;
			addr= align(addr, click_size);

			/* big kernels must be loaded into extended memory */
			if (k_flags & K_KHIGH) {
				addr= mem[1].base;
				limit= mem[1].base + mem[1].size;
			}
		}

		/* Save a copy of the header for the kernel, with a_syms
		 * misused as the address where the process is loaded at.
		 */
		DEBUGEXTRA(("raw_copy(0x%lx, 0x%lx, 0x%x)... ", 
			aout + i * A_MINHDR, mon2abs(&hdr.process), A_MINHDR));
		hdr.process.a_syms= addr;
		raw_copy(aout + i * A_MINHDR, mon2abs(&hdr.process), A_MINHDR);
		DEBUGEXTRA(("done\n"));

		if (!banner) {
			DEBUGBASIC(("     cs       ds     text     data      bss"));
			if (k_flags & K_CHMEM) DEBUGBASIC(("    stack"));
			DEBUGBASIC(("\n"));
			banner= 1;
		}

		/* Segment sizes. */
		DEBUGEXTRA(("a_text=0x%lx; a_data=0x%lx; a_bss=0x%lx; a_flags=0x%x)\n",
			hdr.process.a_text, hdr.process.a_data, 
			hdr.process.a_bss, hdr.process.a_flags));

		a_text= hdr.process.a_text;
		a_data= hdr.process.a_data;
		a_bss= hdr.process.a_bss;
		if (k_flags & K_CHMEM) {
			a_stack= hdr.process.a_total - a_data - a_bss;
			if (!(hdr.process.a_flags & A_SEP)) a_stack-= a_text;
		} else {
			a_stack= 0;
		}

		/* Collect info about the process to be. */
		procp->cs= addr;

		/* Process may be page aligned so that the text segment contains
		 * the header, or have an unmapped zero page against vaxisms.
		 */
		procp->entry= hdr.process.a_entry;
		if (hdr.process.a_flags & A_PAL) a_text+= hdr.process.a_hdrlen;
		if (hdr.process.a_flags & A_UZP) procp->cs-= click_size;

		/* Separate I&D: two segments.  Common I&D: only one. */
		if (hdr.process.a_flags & A_SEP) {
			/* Read the text segment. */
			DEBUGEXTRA(("get_segment(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n",
				vsec, a_text, addr, limit));
			if (!get_segment(&vsec, &a_text, &addr, limit)) return;
			DEBUGEXTRA(("get_segment done vsec=0x%lx a_text=0x%lx "
				"addr=0x%lx\n", 
				vsec, a_text, addr));

			/* The data segment follows. */
			procp->ds= addr;
			if (hdr.process.a_flags & A_UZP) procp->ds-= click_size;
			procp->data= addr;
		} else {
			/* Add text to data to form one segment. */
			procp->data= addr + a_text;
			procp->ds= procp->cs;
			a_data+= a_text;
		}

		/* Read the data segment. */
		DEBUGEXTRA(("get_segment(0x%lx, 0x%lx, 0x%lx, 0x%lx)\n", 
			vsec, a_data, addr, limit));
		if (!get_segment(&vsec, &a_data, &addr, limit)) return;
		DEBUGEXTRA(("get_segment done vsec=0x%lx a_data=0x%lx "
			"addr=0x%lx\n", 
			vsec, a_data, addr));

		/* Make space for bss and stack unless... */
		if (i != KERNEL_IDX && (k_flags & K_CLAIM)) a_bss= a_stack= 0;

		DEBUGBASIC(("%07lx  %07lx %8ld %8ld %8ld",
			procp->cs, procp->ds, hdr.process.a_text,
			hdr.process.a_data, hdr.process.a_bss));
		if (k_flags & K_CHMEM) DEBUGBASIC((" %8ld", a_stack));

		/* Note that a_data may be negative now, but we can look at it
		 * as -a_data bss bytes.
		 */

		/* Compute the number of bss clicks left. */
		a_bss+= a_data;
		n= align(a_bss, click_size);
		a_bss-= n;

		/* Zero out bss. */
		DEBUGEXTRA(("\nraw_clear(0x%lx, 0x%lx); limit=0x%lx... ", addr, n, limit));
		if (addr + n > limit) { errno= ENOMEM; return; }
		raw_clear(addr, n);
		DEBUGEXTRA(("done\n"));
		addr+= n;

		/* And the number of stack clicks. */
		a_stack+= a_bss;
		n= align(a_stack, click_size);
		a_stack-= n;

		/* Add space for the stack. */
		addr+= n;

		/* Process endpoint. */
		procp->end= addr;

		if (verboseboot >= VERBOSEBOOT_BASIC)
			printf("  %s\n", hdr.name);
		else {
			u32_t mem;
			mem = addr-startaddr;
			printf("%s ", hdr.name);
			totalmem += mem;
		}

		if (i == 0 && (k_flags & (K_HIGH | K_KHIGH)) == K_HIGH) {
			/* Load the rest in extended memory. */
			addr= mem[1].base;
			limit= mem[1].base + mem[1].size;
		}
	}

	if (verboseboot < VERBOSEBOOT_BASIC)
		printf("(%luk)\n", totalmem/1024);

	if ((n_procs= i) == 0) {
		printf("There are no programs in %s\n", image);
		errno= 0;
		return;
	}

	/* Check the kernel magic number. */
	raw_copy(mon2abs(&kmagic), 
		process[KERNEL_IDX].data + MAGIC_OFF, sizeof(kmagic));
	if (kmagic != KERNEL_D_MAGIC) {
		printf("Kernel magic number is incorrect (0x%x@0x%lx)\n", 
			kmagic, process[KERNEL_IDX].data + MAGIC_OFF);
		errno= 0;
		return;
	}

	/* Patch sizes, etc. into kernel data. */
	DEBUGEXTRA(("patch_sizes()... "));
	patch_sizes();
	DEBUGEXTRA(("done\n"));

#if !DOS
	if (!(k_flags & K_MEML)) {
		/* Copy the a.out headers to the old place. */
		raw_copy(HEADERPOS, aout, PROCESS_MAX * A_MINHDR);
	}
#endif

	/* Run the trailer function just before starting Minix. */
	DEBUGEXTRA(("run_trailer()... "));
	if (!run_trailer()) { errno= 0; return; }
	DEBUGEXTRA(("done\n"));

	/* Translate the boot parameters to what Minix likes best. */
	DEBUGEXTRA(("params2params(0x%x, 0x%x)... ", params, sizeof(params)));
	if (!params2params(params, sizeof(params))) { errno= 0; return; }
	DEBUGEXTRA(("done\n"));

	/* Set the video to the required mode. */
	if ((console= b_value("console")) == nil || (mode= a2x(console)) == 0) {
		mode= strcmp(b_value("chrome"), "color") == 0 ? COLOR_MODE :
								MONO_MODE;
	}
	DEBUGEXTRA(("set_mode(%d)... ", mode));
	set_mode(mode);
	DEBUGEXTRA(("done\n"));

	/* Close the disk. */
	DEBUGEXTRA(("dev_close()... "));
	(void) dev_close();
	DEBUGEXTRA(("done\n"));

	/* Minix. */
	DEBUGEXTRA(("minix(0x%lx, 0x%lx, 0x%lx, 0x%x, 0x%x, 0x%lx)\n", 
		process[KERNEL_IDX].entry, process[KERNEL_IDX].cs,
		process[KERNEL_IDX].ds, params, sizeof(params), aout));
	minix(process[KERNEL_IDX].entry, process[KERNEL_IDX].cs,
			process[KERNEL_IDX].ds, params, sizeof(params), aout);

	if (!(k_flags & K_BRET)) {
		extern u32_t reboot_code;
		raw_copy(mon2abs(params), reboot_code, sizeof(params));
	}
	parse_code(params);

	/* Return from Minix.  Things may have changed, so assume nothing. */
	fsok= -1;
	errno= 0;

	/* Read leftover character, if any. */
	scan_keyboard();

	/* Restore screen contents. */
	restore_screen();
}



ino_t latest_version(char *version, struct stat *stp)
/* Recursively read the current directory, selecting the newest image on
 * the way up.  (One can't use r_stat while reading a directory.)
 */
{
	char name[MFS_DIRSIZ + 1];
	ino_t ino, newest;
	time_t mtime;

	if ((ino= r_readdir(name)) == 0) { stp->st_mtime= 0; return 0; }

	newest= latest_version(version, stp);
	mtime= stp->st_mtime;
	r_stat(ino, stp);

	if (S_ISREG(stp->st_mode) && stp->st_mtime > mtime) {
		newest= ino;
		strcpy(version, name);
	} else {
		stp->st_mtime= mtime;
	}
	return newest;
}

char *select_image(char *image)
/* Look image up on the filesystem, if it is a file then we're done, but
 * if its a directory then we want the newest file in that directory.  If
 * it doesn't exist at all, then see if it is 'number:number' and get the
 * image from that absolute offset off the disk.
 */
{
	ino_t image_ino;
	struct stat st;

	image= strcpy(malloc((strlen(image) + 1 + MFS_DIRSIZ + 1)
						 * sizeof(char)), image);

	fsok= r_super(&block_size) != 0;
	if (!fsok || (image_ino= r_lookup(ROOT_INO, image)) == 0) {
		char *size;

		if (numprefix(image, &size) && *size++ == ':'
						&& numeric(size)) {
			vir2sec= flat_vir2sec;
			image_off= a2l(image);
			image_sectors= a2l(size);
			strcpy(image, "Minix");
			return image;
		}
		if (!fsok)
			printf("No image selected\n");
		else
			printf("Can't load %s: %s\n", image, unix_err(errno));
		goto bail_out;
	}

	r_stat(image_ino, &st);
	image_bytes = st.st_size;

	if (!S_ISREG(st.st_mode)) {
		char *version= image + strlen(image);
		char dots[MFS_DIRSIZ + 1];

		if (!S_ISDIR(st.st_mode)) {
			printf("%s: %s\n", image, unix_err(ENOTDIR));
			goto bail_out;
		}
		(void) r_readdir(dots);
		(void) r_readdir(dots);	/* "." & ".." */
		*version++= '/';
		*version= 0;
		if ((image_ino= latest_version(version, &st)) == 0) {
			printf("There are no images in %s\n", image);
			goto bail_out;
		}
		r_stat(image_ino, &st);
	}
	vir2sec= file_vir2sec;
	image_sectors= (st.st_size + SECTOR_SIZE - 1) >> SECTOR_SHIFT;
	return image;
bail_out:
	free(image);
	return nil;
}

void bootminix(void)
/* Load Minix and run it.  (Given the size of this program it is surprising
 * that it ever gets to that.)
 */
{
	char *image;
	char *mb;
	char *kernel;
	/* FIXME: modules should come from environment */
	char modules[] = "boot/ds boot/rs boot/pm boot/sched boot/vfs boot/memory boot/log boot/tty boot/mfs boot/vm boot/pfs boot/init";

	if ((mb = b_value("mb")) != nil) {
		do_multiboot = a2l(mb);
		kernel = b_value("kernel");
		if (kernel == nil) {
			printf("kernel not set\n");
			return;
		}
	}

	if (do_multiboot) {
		if ((kernel= select_image(b_value("kernel"))) == nil) return;
	} else {
		if ((image= select_image(b_value("image"))) == nil) return;
	}

	if(serial_line >= 0) {
		char linename[2];
		linename[0] = serial_line + '0';
		linename[1] = '\0';
		b_setvar(E_VAR, SERVARNAME, linename);
	}

	if (do_multiboot)
		exec_mb(kernel, modules);
	else
		exec_image(image);

	switch (errno) {
	case ENOEXEC:
		printf("%s contains a bad program header\n",
			   do_multiboot ? kernel : image);
		break;
	case ENOMEM:
		printf("Not enough memory to load %s\n",
			   do_multiboot ? kernel : image);
		break;
	case EIO:
		printf("Unexpected EOF on %s\n",
			   do_multiboot ? kernel : image);
	case 0:
		/* No error or error already reported. */;
	}

	if (do_multiboot)
		free(kernel);
	else
		free(image);

	if(serial_line >= 0) 
		b_unset(SERVARNAME);
}

size_t
strspn(const char *string, const char *in)
{
	register const char *s1, *s2;

	for (s1 = string; *s1; s1++) {
		for (s2 = in; *s2 && *s2 != *s1; s2++)
			/* EMPTY */ ;
		if (*s2 == '\0')
			break;
	}
	return s1 - string;
}

char *
strpbrk(register const char *string, register const char *brk)
{
	register const char *s1;

	while (*string) {
		for (s1 = brk; *s1 && *s1 != *string; s1++)
			/* EMPTY */ ;
		if (*s1)
			return (char *)string;
		string++;
	}
	return (char *)NULL;
}

char *
strtok(register char *string, const char *separators)
{
	register char *s1, *s2;
	static char *savestring = NULL;

	if (string == NULL) {
		string = savestring;
		if (string == NULL) return (char *)NULL;
	}

	s1 = string + strspn(string, separators);
	if (*s1 == '\0') {
		savestring = NULL;
		return (char *)NULL;
	}

	s2 = strpbrk(s1, separators);
	if (s2 != NULL)
		*s2++ = '\0';
	savestring = s2;
	return s1;
}

char *
strdup(const char *s1)
{
	size_t len;
	char *s2;

	len= strlen(s1)+1;

	s2= malloc(len);
	if (s2 == NULL)
		return NULL;
	strcpy(s2, s1);

	return s2;
}

/*
 * $PchId: bootimage.c,v 1.10 2002/02/27 19:39:09 philip Exp $
 */
