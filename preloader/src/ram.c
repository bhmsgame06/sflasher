#include <string.h>
#include "ram.h"

/* init ram */
void init_ram() {
	/* copying .data to RAM */
	memcpy((void *)&_s_imageaddr_data, (void *)&_s_loadaddr_data, (size_t)&_s_imagesize_data);
	/* zeroing .bss */
	memset((void *)&_s_imageaddr_bss, 0, (size_t)&_s_imagesize_bss);
}
