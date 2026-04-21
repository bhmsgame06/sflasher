#ifndef _RAM_H
#define _RAM_H 1

/* .data section */
extern char _s_loadaddr_data[];
extern char _s_imageaddr_data[];
extern char _s_imagesize_data[];

/* .bss section */
extern char _s_imageaddr_bss[];
extern char _s_imagesize_bss[];

/* init ram */
extern void init_ram();

#endif /* _RAM_H */
