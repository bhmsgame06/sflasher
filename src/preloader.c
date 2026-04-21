#include <stdint.h>

/* preloader loadable code */
const uint8_t preloader_data[] = {
#embed "../preloader/bin/preloader.bin"
};

/* preloader loadable code size */
const uint32_t preloader_data_size = sizeof(preloader_data);
