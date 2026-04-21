#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include "ihex.h"

static bool write_buf(int fd, uint8_t *data, size_t length) {
	uint8_t sbuf[8];
	int sn;
	uint8_t checksum = 0;
	
	sn = sprintf(sbuf, ":");
	if(write(fd, sbuf, sn) < 0)
		return false;

	for(int i = 0; i < length; i++) {
		checksum -= data[i];
		sn = sprintf(sbuf, "%02X", data[i]);
		if(write(fd, sbuf, sn) < 0)
			return false;

	}

	sn = sprintf(sbuf, "%02X\r\n", checksum);
	if(write(fd, sbuf, sn) < 0)
		return false;

	return true;
}

bool create_ihex(int fd, uint32_t start_address, const uint8_t *data, size_t length) {
	uint32_t end_address = start_address + length;
	uint32_t high = 0xffffffff;

	uint8_t tmp_buf[20];

	for(uint32_t offset = start_address; offset < end_address; offset += 16, length -= 16) {
		if((offset >> 16) != high) {
			high = (offset >> 16);
			tmp_buf[0] = 0x02;
			tmp_buf[1] = 0x00;
			tmp_buf[2] = 0x00;
			tmp_buf[3] = 0x04;
			tmp_buf[4] = high >> 8;
			tmp_buf[5] = high;
			if(!write_buf(fd, tmp_buf, 6))
				return false;
		}
		uint16_t chunk_len = tmp_buf[0] = (length < 16) ? length : 16;
		tmp_buf[1] = offset >> 8;
		tmp_buf[2] = offset;
		tmp_buf[3] = 0x00;
		memcpy(&tmp_buf[4], data, chunk_len);
		data += chunk_len;
		if(!write_buf(fd, tmp_buf, 4 + chunk_len))
			return false;
	}

	tmp_buf[0] = 0x04;
	tmp_buf[1] = 0x00;
	tmp_buf[2] = 0x00;
	tmp_buf[3] = 0x05;
	tmp_buf[4] = start_address >> 24;
	tmp_buf[5] = start_address >> 16;
	tmp_buf[6] = start_address >> 8;
	tmp_buf[7] = start_address;
	if(!write_buf(fd, tmp_buf, 8))
		return false;

	tmp_buf[0] = 0x00;
	tmp_buf[1] = 0x00;
	tmp_buf[2] = 0x00;
	tmp_buf[3] = 0x01;
	if(!write_buf(fd, tmp_buf, 4))
		return false;

	return true;
}
