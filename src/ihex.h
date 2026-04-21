#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

extern bool create_ihex(int fd, uint32_t start_address, const uint8_t *data, size_t length);
