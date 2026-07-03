#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <zlib.h>
#include <openssl/evp.h>
#include "tfs.h"

#define CHECK_EOF()	if(cfg_pointer >= cfg_size) return false;

/* .cfg structs */

typedef struct {
	uint8_t dir_name[256];
} CFG_DIR;

typedef struct {
	uint8_t file_name[256];
	uint32_t file_size;
} CFG_FILE;

/* entry structs */

typedef enum {
	TFS4_ENT_TYPE_NULL,
	TFS4_ENT_TYPE_DIR,
	TFS4_ENT_TYPE_COMM,
	TFS4_ENT_TYPE_FILE
} TFS4_ENT_TYPE;

struct tfs4_ctrl_ent_dir {
	uint32_t next;
	uint32_t prev;
	uint32_t comm;
	uint32_t parent_dir;
	uint32_t first;
	uint8_t name[256];
};

struct tfs4_ctrl_ent_comm {
	uint32_t mod_time;
	uint32_t ac_time;
	uint32_t fileno_ffs;
	uint32_t size;
	uint16_t frst_sect;
	uint16_t last_sect;
	uint16_t user_id;
	uint16_t group_id;
	uint32_t cwds;
	uint32_t attrib;
};

struct tfs4_ctrl_ent_file {
	uint32_t next;
	uint32_t prev;
	uint32_t comm;
	uint32_t parent_dir;
	uint8_t name[256];
};

struct tfs4_ctrl_ent {
	TFS4_ENT_TYPE type;
	union {
		struct tfs4_ctrl_ent_dir dir;
		struct tfs4_ctrl_ent_comm comm;
		struct tfs4_ctrl_ent_file file;
	};
};

/* general structs */

typedef enum : uint16_t {
	SECT_UNUSED,
	SECT_FREE,
	SECT_FIRST_DATA,
	SECT_CONT_DATA
} SECT_TYPE;

struct sector {
	SECT_TYPE type;
	uint16_t next;
};

struct tfs4_ctrl_header {
	uint16_t num_sects;
	uint32_t ctrl_size;
	uint32_t seq_num;
	uint32_t total_ent;
	uint32_t ctrl_num_shorts;
	uint32_t fileno_gen;
	uint32_t free_sect;
	uint32_t last_free_sect;
	uint32_t free_sects;
	uint32_t *wear_tbl;
	uint32_t sig;
	uint32_t free_ctrl_sect;
	struct tfs4_ctrl_ent *ent_tbl;
	struct sector *sect_tbl;
};

static uint32_t cfg_line;
static uint32_t cfg_pointer;

static uint8_t tfs_md5_dig[16];
static bool tfs_md5_dig_set;
static uint8_t tfs_version[256];
static bool tfs_version_set;
static uint32_t tfs_num_dir;
static uint32_t tfs_max_files;
static CFG_DIR *tfs_dir_table;
static CFG_FILE *tfs_file_table;

static uint32_t convert_from_ptr(uint32_t encoded) {
	if(encoded == 0xffffffff)
		return 0xffffffff;
	return ((encoded / 20) + 1) | ((encoded % 20) << 16);
}

static uint32_t convert_to_ptr(uint32_t encoded) {
	if(encoded == 0xffffffff)
		return 0xffffffff;
	return (((encoded & 0xffff) - 1) * 20) + (encoded >> 16);
}

/* create an empty allocated control header structure */
static bool init_ctrl_header(struct tfs4_ctrl_header *ctrl_header, uint32_t num_sects) {
	/* initializing struct */
	ctrl_header->num_sects = num_sects;
	ctrl_header->ctrl_size = 0;
	ctrl_header->seq_num = 0;
	ctrl_header->total_ent = 0;
	ctrl_header->ctrl_num_shorts = 0;
	ctrl_header->fileno_gen = 0;
	ctrl_header->free_sect = DEFAULT_DATA_SECT;
	ctrl_header->last_free_sect = num_sects - 1;
	ctrl_header->free_sects = num_sects - DEFAULT_CTRL_SECT;
	ctrl_header->sig = 0x54413130;
	ctrl_header->free_ctrl_sect = DEFAULT_CTRL_SECT;

	/* allocating wear table */
	ctrl_header->wear_tbl = malloc((num_sects >> 8) * sizeof(uint32_t));
	if(!ctrl_header->wear_tbl) {
		return false;
	}
	memset(ctrl_header->wear_tbl, 0x00, (num_sects >> 8) * sizeof(uint32_t));

	/* entry table is NULL yet */
	ctrl_header->ent_tbl = NULL;

	/* allocating sector table */
	ctrl_header->sect_tbl = malloc(num_sects * sizeof(struct sector));
	if(!ctrl_header->sect_tbl) {
		free(ctrl_header->wear_tbl);
		return false;
	}

	/* initializing sector table */
	memset(ctrl_header->sect_tbl, 0x00, num_sects * sizeof(struct sector));
	for(int i = DEFAULT_DATA_SECT; i < num_sects - 1; i++) {
		ctrl_header->sect_tbl[i].next = i + 1;
		ctrl_header->sect_tbl[i].type = SECT_FREE;
	}
	ctrl_header->sect_tbl[num_sects - 1].next = 0xfffc;
	ctrl_header->sect_tbl[num_sects - 1].type = SECT_FREE;

	return true;
}

/* create control header structure from a raw byte array */
static bool parse_ctrl_header(struct tfs4_ctrl_header *ctrl_header, const uint8_t *ctrl, uint32_t num_sects) {
	/* common info */
	ctrl_header->num_sects = num_sects;
	ctrl_header->ctrl_size = ((uint32_t *)ctrl)[0];
	ctrl_header->seq_num = ((uint32_t *)ctrl)[1];
	ctrl_header->total_ent = ((uint32_t *)ctrl)[2];
	ctrl_header->ctrl_num_shorts = ((uint32_t *)ctrl)[3];
	ctrl_header->fileno_gen = ((uint32_t *)ctrl)[4];
	ctrl_header->free_sect = ((uint32_t *)ctrl)[5];
	ctrl_header->last_free_sect = ((uint32_t *)ctrl)[6];
	ctrl_header->free_sects = ((uint32_t *)ctrl)[7];
	ctrl_header->wear_tbl = malloc((num_sects >> 8) * sizeof(uint32_t));
	if(!ctrl_header->wear_tbl) {
		return false;
	}

	/* wear table */
	uint32_t high_wear = ((uint32_t *)ctrl)[8];
	ctrl += 9 * sizeof(uint32_t);
	for(int i = 0; i < num_sects >> 8; i += 2) {
		uint8_t two_offsets = *ctrl++;
		ctrl_header->wear_tbl[i] = high_wear - (two_offsets >> 4);
		if(i + 1 < num_sects >> 8) ctrl_header->wear_tbl[i + 1] = high_wear - (two_offsets & 0xf);
	}
	
	ctrl_header->sig = ((uint32_t *)ctrl)[0];
	ctrl_header->free_ctrl_sect = ((uint32_t *)ctrl)[1];
	ctrl += 2 * sizeof(uint32_t);

	/* entry table */
	ctrl_header->ent_tbl = malloc(ctrl_header->total_ent * sizeof(struct tfs4_ctrl_ent));
	if(!ctrl_header->ent_tbl) {
		free(ctrl_header->wear_tbl);
		return false;
	}
	for(int i = 0; i < ctrl_header->total_ent; i++) {
		switch(ctrl_header->ent_tbl[i].type = *ctrl++) {
			case TFS4_ENT_TYPE_DIR: {
				ctrl_header->ent_tbl[i].dir.next = convert_to_ptr(*(uint32_t *)ctrl);
				ctrl_header->ent_tbl[i].dir.prev = convert_to_ptr(*(uint32_t *)(ctrl + 4));
				ctrl_header->ent_tbl[i].dir.comm = convert_to_ptr(*(uint32_t *)(ctrl + 8));
				ctrl_header->ent_tbl[i].dir.parent_dir = convert_to_ptr(*(uint32_t *)(ctrl + 12));
				ctrl_header->ent_tbl[i].dir.first = convert_to_ptr(*(uint32_t *)(ctrl + 16));
				ctrl += 20;
				int k;
				char c;
				for(k = 0; c = *ctrl++; k++)
					ctrl_header->ent_tbl[i].dir.name[k] = c;
				ctrl_header->ent_tbl[i].dir.name[k] = '\0';
				break;
			}

			case TFS4_ENT_TYPE_COMM: {
				ctrl_header->ent_tbl[i].comm.mod_time = *(uint32_t *)(ctrl + 4);
				ctrl_header->ent_tbl[i].comm.ac_time = *(uint32_t *)(ctrl + 8);
				ctrl_header->ent_tbl[i].comm.fileno_ffs = *(uint32_t *)(ctrl + 12);
				ctrl_header->ent_tbl[i].comm.size = *(uint32_t *)(ctrl + 16);
				ctrl_header->ent_tbl[i].comm.frst_sect = *(uint16_t *)(ctrl + 20);
				ctrl_header->ent_tbl[i].comm.last_sect = *(uint16_t *)(ctrl + 22);
				ctrl_header->ent_tbl[i].comm.user_id = *(uint16_t *)(ctrl + 24);
				ctrl_header->ent_tbl[i].comm.group_id = *(uint16_t *)(ctrl + 26);
				ctrl_header->ent_tbl[i].comm.cwds = *(uint32_t *)(ctrl + 28);
				ctrl_header->ent_tbl[i].comm.attrib = *(uint32_t *)(ctrl + 33);
				ctrl += 38;
				break;
			}

			case TFS4_ENT_TYPE_FILE: {
				ctrl_header->ent_tbl[i].file.next = convert_to_ptr(*(uint32_t *)ctrl);
				ctrl_header->ent_tbl[i].file.prev = convert_to_ptr(*(uint32_t *)(ctrl + 4));
				ctrl_header->ent_tbl[i].file.comm = convert_to_ptr(*(uint32_t *)(ctrl + 8));
				ctrl_header->ent_tbl[i].file.parent_dir = convert_to_ptr(*(uint32_t *)(ctrl + 12));
				ctrl += 16;
				int k;
				char c;
				for(k = 0; c = *ctrl++; k++)
					ctrl_header->ent_tbl[i].file.name[k] = c;
				ctrl_header->ent_tbl[i].file.name[k] = '\0';
				break;
			}
		}
	}

	ctrl_header->sect_tbl = malloc(num_sects * sizeof(struct sector));
	if(!ctrl_header->sect_tbl) {
		free(ctrl_header->wear_tbl);
		free(ctrl_header->ent_tbl);
		return false;
	}
	memset(ctrl_header->sect_tbl, 0x00, num_sects * sizeof(struct sector));

	/* free sector table */
	uint16_t last_sect;
	for(int i = 0; ; i++) {
		uint16_t sect;
		if(!i) {
			sect = ctrl_header->free_sect;
		} else {
			ctrl_header->sect_tbl[last_sect].type = SECT_FREE;
			sect = ctrl_header->sect_tbl[last_sect].next = *(uint16_t *)ctrl;
			ctrl += sizeof(uint16_t);
			if(sect == 0xfffc)
				break;
		}

		last_sect = *(uint16_t *)ctrl;
		ctrl += sizeof(uint16_t);

		for(int k = sect; k < last_sect; k++) {
			ctrl_header->sect_tbl[k].type = SECT_FREE;
			ctrl_header->sect_tbl[k].next = k + 1;
		}
	}

	/* general sector table */
	while(true) {
		uint16_t sect = *(uint16_t *)ctrl;
		ctrl += sizeof(uint16_t);
		if(sect == 0xfffc)
			break;

		for(bool first = true; ; ) {
			uint16_t next = *(uint16_t *)ctrl;
			ctrl += sizeof(uint16_t);

			if(first) {
				first = false;
				ctrl_header->sect_tbl[sect].type = SECT_FIRST_DATA;
			} else {
				ctrl_header->sect_tbl[sect].type = SECT_CONT_DATA;
			}

			if(next == 0xfffc) {
				ctrl_header->sect_tbl[sect].next = next;
				break;
			} else if(next == 0xfffe) {
				next = *(uint16_t *)ctrl;
				ctrl += sizeof(uint16_t);
				for(; sect < next; sect++) {
					ctrl_header->sect_tbl[sect + 1].type = SECT_CONT_DATA;
					ctrl_header->sect_tbl[sect].next = sect + 1;
				}
			} else {
				ctrl_header->sect_tbl[sect].next = next;
				sect = next;
			}
		}
	}

	FILE *fd = fopen("/home/bhms/sect_tbl.dbg", "wb");
	fwrite(ctrl_header->sect_tbl, sizeof(struct sector), 0x7800, fd);
	fclose(fd);

	return true;
}

/* update ctrl_header->ctrl_size and ctrl_header->ctrl_num_shorts */
static void update_ctrl_sizes(struct tfs4_ctrl_header *ctrl_header) {
	uint32_t ctrl_size = (ctrl_header->num_sects >> 9) + ((ctrl_header->num_sects >> 8) & 1) + 44;

	/* entry table */
	for(int i = 0; i < ctrl_header->total_ent; i++) {
		ctrl_size++;
		switch(ctrl_header->ent_tbl[i].type) {
			case TFS4_ENT_TYPE_DIR:
				ctrl_size += strlen(ctrl_header->ent_tbl[i].dir.name) + 21;
				break;

			case TFS4_ENT_TYPE_COMM:
				ctrl_size += 38;
				break;

			case TFS4_ENT_TYPE_FILE:
				ctrl_size += strlen(ctrl_header->ent_tbl[i].file.name) + 17;
				break;
		}
	}

	uint32_t ctrl_num_shorts = 0;

	uint16_t sect = ctrl_header->free_sect;
	uint16_t last_sect = sect;

	/* free sector table */
loop:
	while(true) {
		if(sect != ctrl_header->free_sect) ctrl_num_shorts++;

		while(true) {
			if((sect = ctrl_header->sect_tbl[sect].next) == 0xfffc) {
				ctrl_num_shorts += 2;
				break loop;
			} else if(sect != last_sect + 1) {
				break;
			}
			last_sect = sect;
		}

		ctrl_num_shorts++;
		last_sect = sect;
	}

	/* general sector table */
	for(int i = 0; i < ctrl_header->num_sects; i++) {
		if(ctrl_header->sect_tbl[i].type == SECT_FIRST_DATA) {
			sect = last_sect = i;
			ctrl_num_shorts++;

			for(bool b = false; ; ) {
				sect = ctrl_header->sect_tbl[sect].next;

				if(sect != last_sect + 1) {
					if(b) {
						ctrl_num_shorts += 2;
						b = false;
					}
					ctrl_num_shorts++;
				} else {
					b = true;
				}

				if(sect == 0xfffc)
					break;

				last_sect = sect;
			}
		}
	}

	int num_sects = (ctrl_size + (ctrl_num_shorts << 1));
	num_sects = (num_sects / (TFS_SECT_SIZE)) + ((num_sects % (TFS_SECT_SIZE)) != 0);

	ctrl_num_shorts += 2 + num_sects;

	ctrl_size += ctrl_num_shorts << 1;

	ctrl_header->ctrl_size = ctrl_size;
	ctrl_header->ctrl_num_shorts = ctrl_num_shorts;
}

/* build a final byte array from the control header structure */
static uint8_t *build_ctrl_header(struct tfs4_ctrl_header *ctrl_header) {
	uint8_t *ctrl = malloc(ctrl_header->ctrl_size);
	memset(ctrl, 0xff, ctrl_header->ctrl_size);
	uint8_t *p_ctrl = ctrl;

	*(uint32_t *)p_ctrl = ctrl_header->ctrl_size;
	*(uint32_t *)(p_ctrl + 4) = ctrl_header->seq_num;
	*(uint32_t *)(p_ctrl + 8) = ctrl_header->total_ent;
	*(uint32_t *)(p_ctrl + 12) = ctrl_header->ctrl_num_shorts;
	*(uint32_t *)(p_ctrl + 16) = ctrl_header->fileno_gen;
	*(uint32_t *)(p_ctrl + 20) = ctrl_header->free_sect;
	*(uint32_t *)(p_ctrl + 24) = ctrl_header->last_free_sect;
	*(uint32_t *)(p_ctrl + 28) = ctrl_header->free_sects;

	uint8_t dummy;

	/* wear table */
	uint32_t high_wear = 0;
	for(int i = 0; i < ctrl_header->num_sects >> 8; i++) {
		if(ctrl_header->wear_tbl[i] > high_wear)
			high_wear = ctrl_header->wear_tbl[i];
	}

	*(uint32_t *)(p_ctrl + 32) = high_wear;
	p_ctrl += 36;

	for(int i = 0; i < ctrl_header->num_sects >> 8; i += 2) {
		uint8_t off1 = high_wear - ctrl_header->wear_tbl[i];
		uint8_t off2 = high_wear - ctrl_header->wear_tbl[i + 1];

		dummy = *p_ctrl++ = ((off1 > 0xf ? 0xf : off1) << 4) | (off2 > 0xf ? 0xf : off2);
	}

	*(uint32_t *)p_ctrl = ctrl_header->sig;
	*(uint32_t *)(p_ctrl + 4) = ctrl_header->free_ctrl_sect;

	p_ctrl += 8;

	/* entry table */
	for(int i = 0; i < ctrl_header->total_ent; i++) {
		uint8_t type = *p_ctrl++ = ctrl_header->ent_tbl[i].type;
		switch(type) {
			case TFS4_ENT_TYPE_DIR: {
				*(uint32_t *)p_ctrl = convert_from_ptr(ctrl_header->ent_tbl[i].dir.next);
				*(uint32_t *)(p_ctrl + 4) = convert_from_ptr(ctrl_header->ent_tbl[i].dir.prev);
				*(uint32_t *)(p_ctrl + 8) = convert_from_ptr(ctrl_header->ent_tbl[i].dir.comm);
				*(uint32_t *)(p_ctrl + 12) = convert_from_ptr(ctrl_header->ent_tbl[i].dir.parent_dir);
				*(uint32_t *)(p_ctrl + 16) = convert_from_ptr(ctrl_header->ent_tbl[i].dir.first);
				p_ctrl += 20;
				for(int k = 0; ; k++) {
					char c;
					*p_ctrl++ = (c = ctrl_header->ent_tbl[i].dir.name[k]);
					if(c == '\0')
						break;
				}
				break;
			}

			case TFS4_ENT_TYPE_COMM: {
				*(uint16_t *)p_ctrl = ctrl_header->ent_tbl[i].comm.last_sect;
				uint32_t dig = ctrl_header->ent_tbl[i].comm.size;
				while(dig >= TFS_PAGE_SIZE) {
					dig = (dig & 0x1ff) + (dig >> 9);
				}
				*(uint16_t *)(p_ctrl + 2) = dig;
				*(uint32_t *)(p_ctrl + 4) = ctrl_header->ent_tbl[i].comm.mod_time;
				*(uint32_t *)(p_ctrl + 8) = ctrl_header->ent_tbl[i].comm.ac_time;
				*(uint32_t *)(p_ctrl + 12) = ctrl_header->ent_tbl[i].comm.fileno_ffs;
				*(uint32_t *)(p_ctrl + 16) = ctrl_header->ent_tbl[i].comm.size;
				*(uint16_t *)(p_ctrl + 20) = ctrl_header->ent_tbl[i].comm.frst_sect;
				*(uint16_t *)(p_ctrl + 22) = ctrl_header->ent_tbl[i].comm.last_sect;
				*(uint16_t *)(p_ctrl + 24) = ctrl_header->ent_tbl[i].comm.user_id;
				*(uint16_t *)(p_ctrl + 26) = ctrl_header->ent_tbl[i].comm.group_id;
				*(uint32_t *)(p_ctrl + 28) = ctrl_header->ent_tbl[i].comm.cwds;
				*(uint8_t *)(p_ctrl + 32) = 1;
				*(uint32_t *)(p_ctrl + 33) = ctrl_header->ent_tbl[i].comm.attrib;
				*(uint8_t *)(p_ctrl + 37) = dummy;
				p_ctrl += 38;
				break;
			}

			case TFS4_ENT_TYPE_FILE: {
				*(uint32_t *)p_ctrl = convert_from_ptr(ctrl_header->ent_tbl[i].file.next);
				*(uint32_t *)(p_ctrl + 4) = convert_from_ptr(ctrl_header->ent_tbl[i].file.prev);
				*(uint32_t *)(p_ctrl + 8) = convert_from_ptr(ctrl_header->ent_tbl[i].file.comm);
				*(uint32_t *)(p_ctrl + 12) = convert_from_ptr(ctrl_header->ent_tbl[i].file.parent_dir);
				p_ctrl += 16;
				for(int k = 0; ; k++) {
					char c;
					*p_ctrl++ = (c = ctrl_header->ent_tbl[i].file.name[k]);
					if(c == '\0')
						break;
				}
				break;
			}
		}
	}


	uint16_t sect = ctrl_header->free_sect;
	uint16_t last_sect = sect;

	/* free sector table */
loop:
	while(true) {
		if(sect != ctrl_header->free_sect) {
			*(uint16_t *)p_ctrl = sect;
			p_ctrl += sizeof(uint16_t);
		}

		while(true) {
			if((sect = ctrl_header->sect_tbl[sect].next) == 0xfffc) {
				*(uint32_t *)p_ctrl = 0xfffc0000 | last_sect;
				p_ctrl += sizeof(uint32_t);
				break loop;
			} else if(sect != last_sect + 1) {
				break;
			}
			last_sect = sect;
		}

		*(uint16_t *)p_ctrl = last_sect;
		p_ctrl += sizeof(uint16_t);

		last_sect = sect;
	}

	/* general sector table */
	for(int i = 0; i < ctrl_header->num_sects; i++) {
		if(ctrl_header->sect_tbl[i].type == SECT_FIRST_DATA) {
			sect = last_sect = i;
			*(uint16_t *)p_ctrl = sect;
			p_ctrl += sizeof(uint16_t);

			for(bool b = false; ; ) {
				sect = ctrl_header->sect_tbl[sect].next;

				if(sect != last_sect + 1) {
					if(b) {
						*(uint32_t *)p_ctrl = 0xfffe | (last_sect << 16);
						p_ctrl += sizeof(uint32_t);
						b = false;
					}
					*(uint16_t *)p_ctrl = sect;
					p_ctrl += sizeof(uint16_t);
				} else {
					b = true;
				}

				if(sect == 0xfffc)
					break;

				last_sect = sect;
			}
		}
	}

	*(uint16_t *)p_ctrl = 0xfffc;

	return ctrl;
}

/* release the memory allocated for the control header structure */
static void free_ctrl_header(struct tfs4_ctrl_header *ctrl_header) {
	free(ctrl_header->wear_tbl);
	free(ctrl_header->ent_tbl);
	free(ctrl_header->sect_tbl);
}

/* current local time (on your PC btw :3, keep in mind) */
static uint32_t get_fat_timestamp_now() {
	time_t raw_time;
	time(&raw_time);
	struct tm *time_info = localtime(&raw_time);
	return (((time_info->tm_year - 80) & 0x7f) << 25) |
			(((time_info->tm_mon + 1) & 0xf) << 21) |
			((time_info->tm_mday & 0x1f) << 16) |
			((time_info->tm_hour & 0x1f) << 11) |
			((time_info->tm_min & 0x3f) << 5) |
			((time_info->tm_sec >> 1) & 0x1f);
}

/* find a dirty block to erase it and reuse */
static int find_dirty_block(struct tfs4_ctrl_header *ctrl_header, uint8_t *sect_marks) {
	for(uint16_t sect = 0; sect < ctrl_header->num_sects; sect += 256) {

		for(int dirty_sects = 0, sub_sect = 0; sub_sect < 256; sub_sect++) {
			if(sect_marks[sect + sub_sect]) {
				goto miss;
			}
			/* hit */
			return sect >> 8;
miss:
		}

	}

	return -1;
}

/* erase dirty blocks and reuse them */
static bool flash_clean(struct tfs4_ctrl_header *ctrl_header, uint8_t *sect_marks) {
	for(bool first = false; ; first = true) {
		int blk = find_dirty_block(ctrl_header, sect_marks);
		if(blk < 0) {
			if(!first) {
				printf("TFS: No free space!\n");
				return false;
			}
			return true;
		}

		uint16_t free_sect = 0xfffc;
		uint16_t last_free_sect = ctrl_header->last_free_sect;
		for(uint16_t sect = blk << 8; sect < ((blk << 8) + 256); sect++) {
			sect_marks[sect] = 0xff;
			ctrl_header->free_sects++;

			if(free_sect == 0xfffc)
				free_sect = sect;
			ctrl_header->sect_tbl[sect].type = SECT_FREE;

			if(last_free_sect != 0xfffc)
				ctrl_header->sect_tbl[last_free_sect].next = sect;
			last_free_sect = sect;
		}

		ctrl_header->wear_tbl[blk]++;

		ctrl_header->sect_tbl[last_free_sect].next = 0xfffc;

		ctrl_header->free_sect = free_sect;
		ctrl_header->last_free_sect = last_free_sect;
	}
}

/* find a free block for the control area */
static int find_free_block(struct tfs4_ctrl_header *ctrl_header) {
	for(uint16_t sect = 0; sect < ctrl_header->num_sects; sect += 256) {

		for(uint16_t sub_sect = 0; sub_sect < 256; sub_sect++) {
			if(ctrl_header->sect_tbl[sect + sub_sect].type != SECT_FREE)
				goto miss;
		}
		/* hit */
		return sect >> 8;
miss:
	}

	return -1;
}

/* occupy exactly 256 sectors (1 block) for the control area */
static void occupy_free_block(struct tfs4_ctrl_header *ctrl_header, int blk) {
	uint16_t sect_start = blk << 8;
	uint16_t sect_end = sect_start + 255;

	/* adjust the linked list */
	uint16_t prev = 0xfffc;
	for(uint16_t sect = ctrl_header->free_sect; ; prev = sect, sect = ctrl_header->sect_tbl[sect].next) {
		if(sect == sect_start)
			break;
	}
	if(prev != 0xfffc)
		ctrl_header->sect_tbl[prev].next = ctrl_header->sect_tbl[sect_end].next;

	/* set to all 256 sectors SECT_UNUSED */
	for(uint16_t sect = sect_start; sect <= sect_end; sect++) {
		ctrl_header->sect_tbl[sect].type = SECT_UNUSED;
	}
}

/* find last free sector in ctrl_header->sect_tbl
 * and update ctrl_header->last_free_sect */
static void update_last_free_sect(struct tfs4_ctrl_header *ctrl_header) {
	for(uint16_t sect = 0; sect < ctrl_header->num_sects; sect++) {
		if(ctrl_header->sect_tbl[sect].type == SECT_FREE &&
				ctrl_header->sect_tbl[sect].next == 0xfffc) {
			ctrl_header->last_free_sect = sect;
			break;
		}
	}
}

/* add sector action to action list */
static bool add_sect_action(struct sect_action_list *act_list, struct sect_action *act) {
	struct sect_action *new_acts = realloc(act_list->acts, (act_list->num_acts + 1) * sizeof(struct sect_action));
	if(!new_acts)
		return false;
	act_list->acts = new_acts;

	memcpy(&new_acts[act_list->num_acts++], act, sizeof(struct sect_action));

	return true;
}

/* add entry to the control header structure */
static uint32_t add_entry(struct tfs4_ctrl_header *ctrl_header, struct tfs4_ctrl_ent *ent) {
	/* finding empty entry */
	uint32_t new_ent;
	for(new_ent = 0;
			new_ent < ctrl_header->total_ent && ctrl_header->ent_tbl[new_ent].type != TFS4_ENT_TYPE_NULL;
			new_ent++) {
	}

	/* not enough empty entries */
	if(new_ent == ctrl_header->total_ent) {
		struct tfs4_ctrl_ent *ent_tbl = realloc(ctrl_header->ent_tbl, (ctrl_header->total_ent += 20) * sizeof(struct tfs4_ctrl_ent));
		if(!ent_tbl)
			return 0xffffffff;
		ctrl_header->ent_tbl = ent_tbl;

		for(int i = new_ent + 1; i < ctrl_header->total_ent; i++) {
			ent_tbl[i].type = TFS4_ENT_TYPE_NULL;
		}
	}

	/* removing the references to this new entry */
	for(int i = 0; i < ctrl_header->total_ent; i++) {
		if(ctrl_header->ent_tbl[i].type == TFS4_ENT_TYPE_DIR) {
			if(ctrl_header->ent_tbl[i].dir.next == new_ent)
				ctrl_header->ent_tbl[i].dir.next = 0xffffffff;
			if(ctrl_header->ent_tbl[i].dir.prev == new_ent)
				ctrl_header->ent_tbl[i].dir.prev = 0xffffffff;
		} else if(ctrl_header->ent_tbl[i].type == TFS4_ENT_TYPE_FILE) {
			if(ctrl_header->ent_tbl[i].file.next == new_ent)
				ctrl_header->ent_tbl[i].file.next = 0xffffffff;
			if(ctrl_header->ent_tbl[i].file.prev == new_ent)
				ctrl_header->ent_tbl[i].file.prev = 0xffffffff;
		}
	}

	memcpy(&ctrl_header->ent_tbl[new_ent], ent, sizeof(struct tfs4_ctrl_ent));

	return new_ent;
}

/* create directory (add dir+comm entry pair to the control header structure) */
static bool create_dir(struct tfs4_ctrl_header *ctrl_header, char *path) {
	char tmp_name[256];
	strcpy(tmp_name, path);
	char *sub = strtok(tmp_name, "/");
	char *dir_name;
	uint32_t parent_dir = 0xffffffff;
	uint32_t found = 0xffffffff;

	do {
		char *next_sub = strtok(NULL, "/");
		for(int i = 0; i < ctrl_header->total_ent; i++) {
			if(ctrl_header->ent_tbl[i].type == TFS4_ENT_TYPE_DIR &&
					ctrl_header->ent_tbl[i].dir.parent_dir == parent_dir &&
					!strcmp(ctrl_header->ent_tbl[i].dir.name, sub)) {
				parent_dir = i;
				if(!next_sub) found = i;
				break;
			}
		}
		dir_name = sub;
		sub = next_sub;
	} while(sub);

	if(found != 0xffffffff) {
		printf("Skip directory: %s\n", path);
		return true;
	}

	printf("Create directory: %s\n", path);

	uint32_t first;
	if(parent_dir != 0xffffffff)
		first = ctrl_header->ent_tbl[parent_dir].dir.first;
	else
		first = 0xffffffff;

	uint32_t timestamp_now = get_fat_timestamp_now();

	struct tfs4_ctrl_ent dir_ent = {
		.type = TFS4_ENT_TYPE_DIR,
		.dir = {
			.next = first,
			.prev = 0xffffffff,
			.parent_dir = parent_dir,
			.first = 0xffffffff
		}
	};
	strncpy(dir_ent.dir.name, dir_name, sizeof(dir_ent.dir.name));
	struct tfs4_ctrl_ent comm_ent = {
		.type = TFS4_ENT_TYPE_COMM,
		.comm = {
			.mod_time = timestamp_now,
			.ac_time = timestamp_now,
			.fileno_ffs = ctrl_header->fileno_gen++,
			.size = 0,
			.frst_sect = 0xffff,
			.last_sect = 0xffff,
			.user_id = 1,
			.group_id = 1,
			.cwds = 0x7,
			.attrib = 0
		}
	};

	uint32_t new_dir_ent = add_entry(ctrl_header, &dir_ent);
	if(new_dir_ent == 0xffffffff)
		return false;

	uint32_t new_comm_ent = add_entry(ctrl_header, &comm_ent);
	if(new_comm_ent == 0xffffffff)
		return false;

	ctrl_header->ent_tbl[new_dir_ent].dir.comm = new_comm_ent;

	if(first != 0xffffffff) {
		if(ctrl_header->ent_tbl[first].type == TFS4_ENT_TYPE_DIR)
			ctrl_header->ent_tbl[first].dir.prev = new_dir_ent;
		else
			ctrl_header->ent_tbl[first].file.prev = new_dir_ent;
	}

	if(parent_dir != 0xffffffff) ctrl_header->ent_tbl[parent_dir].dir.first = new_dir_ent;

	return true;
}

/* create file (add file+comm entry pair to the control header structure) */
static bool create_file(struct tfs4_ctrl_header *ctrl_header, char *path, uint8_t *file_data, uint32_t file_size, struct sect_action_list *act_list, uint8_t *sect_marks) {
	int file_sect_num = (file_size / (TFS_SECT_SIZE)) + (file_size % (TFS_SECT_SIZE) != 0);

	uint16_t frst_sect = ctrl_header->free_sect;
	uint16_t last_sect;

	uint8_t *p_file_data = file_data;
	uint32_t remaining = file_size;

	for(int i = 0; i < file_sect_num; i++) {
		SECT_ACTION_TYPE act_type;

		if(ctrl_header->free_sect >= 0xfffc) {
			if(!flash_clean(ctrl_header, sect_marks))
				return false;
			act_type = SECT_ACTION_ERASE_AND_WRITE;
		} else {
			act_type = SECT_ACTION_WRITE;
		}

		if(!i)
			ctrl_header->sect_tbl[ctrl_header->free_sect].type = SECT_FIRST_DATA;
		else
			ctrl_header->sect_tbl[ctrl_header->free_sect].type = SECT_CONT_DATA;

		struct sect_action new_act = {
			.type = act_type,
			.sect_num = ctrl_header->free_sect
		};
		memset(new_act.sect_data, 0x00, TFS_PAGE_SIZE);
		memcpy(new_act.sect_data, p_file_data, remaining < (TFS_SECT_SIZE) ? remaining : (TFS_SECT_SIZE));
		p_file_data += TFS_SECT_SIZE;
		remaining -= TFS_SECT_SIZE;

		add_sect_action(act_list, &new_act);

		uint16_t next_free = ctrl_header->sect_tbl[ctrl_header->free_sect].next;
		if(i == file_sect_num - 1) {
			ctrl_header->sect_tbl[ctrl_header->free_sect].next = 0xfffc;
			last_sect = ctrl_header->free_sect;
		}

		ctrl_header->free_sect = next_free;
		ctrl_header->free_sects--;
	}

	update_last_free_sect(ctrl_header);

	char tmp_name[256];
	strcpy(tmp_name, path);
	char *sub = strtok(tmp_name, "/");
	char *file_name;
	uint32_t parent_dir = 0xffffffff;
	uint32_t found = 0xffffffff;

	do {
		char *next_sub = strtok(NULL, "/");
		for(int i = 0; i < ctrl_header->total_ent; i++) {

			if(ctrl_header->ent_tbl[i].type == TFS4_ENT_TYPE_DIR) {
				if(ctrl_header->ent_tbl[i].dir.parent_dir == parent_dir &&
					!strcmp(ctrl_header->ent_tbl[i].dir.name, sub)) {
					parent_dir = i;
					if(!next_sub) found = i;
					break;
				}
			} else {
				if(ctrl_header->ent_tbl[i].file.parent_dir == parent_dir &&
					!strcmp(ctrl_header->ent_tbl[i].file.name, sub)) {
					parent_dir = i;
					if(!next_sub) found = i;
					break;
				}
			}

		}
		file_name = sub;
		sub = next_sub;
	} while(sub);

	uint32_t timestamp_now = get_fat_timestamp_now();

	if(found != 0xffffffff) {
		printf("Update file: %s (%d)\n", path, file_size);

		uint32_t comm = ctrl_header->ent_tbl[found].file.comm;

		uint16_t sect = ctrl_header->ent_tbl[comm].comm.frst_sect;

		ctrl_header->ent_tbl[comm].comm.mod_time = timestamp_now;
		ctrl_header->ent_tbl[comm].comm.size = file_size;
		ctrl_header->ent_tbl[comm].comm.frst_sect = frst_sect;
		ctrl_header->ent_tbl[comm].comm.last_sect = last_sect;
	} else {
		printf("Create file: %s (%d)\n", path, file_size);

		uint32_t first;
		if(parent_dir != 0xffffffff)
			first = ctrl_header->ent_tbl[parent_dir].dir.first;
		else
			first = 0xffffffff;

		struct tfs4_ctrl_ent file_ent = {
			.type = TFS4_ENT_TYPE_FILE,
			.file = {
				.next = first,
				.prev = 0xffffffff,
				.parent_dir = parent_dir
			}
		};
		strncpy(file_ent.file.name, file_name, sizeof(file_ent.file.name));
		struct tfs4_ctrl_ent comm_ent = {
			.type = TFS4_ENT_TYPE_COMM,
			.comm = {
				.mod_time = timestamp_now,
				.ac_time = timestamp_now,
				.fileno_ffs = ctrl_header->fileno_gen++,
				.size = file_size,
				.frst_sect = file_size != 0 ? frst_sect : 0xffff,
				.last_sect = file_size != 0 ? last_sect : 0xffff,
				.user_id = 1,
				.group_id = 1,
				.cwds = 0x777,
				.attrib = 0
			}
		};

		uint32_t new_file_ent = add_entry(ctrl_header, &file_ent);
		if(new_file_ent == 0xffffffff)
			return false;

		uint32_t new_comm_ent = add_entry(ctrl_header, &comm_ent);
		if(new_comm_ent == 0xffffffff)
			return false;

		ctrl_header->ent_tbl[new_file_ent].file.comm = new_comm_ent;

		if(first != 0xffffffff) {
			if(ctrl_header->ent_tbl[first].type == TFS4_ENT_TYPE_DIR)
				ctrl_header->ent_tbl[first].dir.prev = new_file_ent;
			else
				ctrl_header->ent_tbl[first].file.prev = new_file_ent;
		}

		if(parent_dir != 0xffffffff) ctrl_header->ent_tbl[parent_dir].dir.first = new_file_ent;
	}

	return true;
}

/* this function is initializing control header structure,
 * adding directories and files to it, building raw control
 * byte array and then fills act_list */
static bool patch_start(uint8_t *tfs_version, uint8_t *tfs, uint32_t tfs_size, uint32_t num_sects, struct sect_action_list *act_list, uint8_t *ctrl, uint8_t *sect_marks, bool update_tfs_version_code) {
	/* initializing tfs4_ctrl_header struct */
	struct tfs4_ctrl_header ctrl_header;
	if(ctrl) {
		if(!parse_ctrl_header(&ctrl_header, ctrl, num_sects))
			return false;
	} else {
		if(!init_ctrl_header(&ctrl_header, num_sects))
			return false;
	}

	/* adding directories */
	for(int i = 0; i < tfs_num_dir; i++) {
		if(!create_dir(&ctrl_header, tfs_dir_table[i].dir_name))
			return false;
	}

	uint8_t *p_tfs = tfs;

	/* creating files */
	if(update_tfs_version_code)
		create_file(&ctrl_header, "/a/tfsVersionCode.tfs", tfs_version, strlen(tfs_version), act_list, sect_marks);

	for(int i = 0; i < tfs_max_files; i++) {
		if(!create_file(&ctrl_header, tfs_file_table[i].file_name, p_tfs, tfs_file_table[i].file_size, act_list, sect_marks))
			return false;
		p_tfs += tfs_file_table[i].file_size;
	}

	/* invalidate all control sectors */
	for(int i = 0; i < num_sects; i++) {
		if(sect_marks[i] == 0xf0) {
			sect_marks[i] = 0x00;
			struct sect_action inv_act = {
				.type = SECT_ACTION_MARK,
				.sect_num = i
			};
			inv_act.sect_data[TFS_SECT_SIZE] = 0;
			add_sect_action(act_list, &inv_act);
		}
	}

	/* adding control sectors to sect_tbl */
	update_ctrl_sizes(&ctrl_header);
	uint32_t remaining = ctrl_header.ctrl_size;
	int sect_num = (remaining / (TFS_SECT_SIZE)) + (remaining % (TFS_SECT_SIZE) != 0);

	uint16_t new_ctrl_sects[sect_num];
	SECT_ACTION_TYPE act_types[sect_num];
	memset(act_types, SECT_ACTION_WRITE, sect_num * sizeof(SECT_ACTION_TYPE));

	for(int i = 0; i < sect_num; i++, ctrl_header.free_ctrl_sect++) {
		if(i != 0 &&
				(ctrl_header.free_ctrl_sect & 0xff) == 0) {

			int free_blk = find_free_block(&ctrl_header);
			if(free_blk < 0) {
				if(!flash_clean(&ctrl_header, sect_marks))
					return false;
				act_types[i] = SECT_ACTION_ERASE_AND_WRITE;

				free_blk = find_free_block(&ctrl_header);
				if(free_blk < 0) {
					printf("TFS: control fragmentation fault\n");
					return false;
				}
			}
			
			occupy_free_block(&ctrl_header, free_blk);

			ctrl_header.free_ctrl_sect = free_blk << 8;

		}

		sect_marks[new_ctrl_sects[i] = ctrl_header.free_ctrl_sect] = 0xf0;

		if(!i)
			ctrl_header.sect_tbl[ctrl_header.free_ctrl_sect].type = SECT_FIRST_DATA;
		else
			ctrl_header.sect_tbl[ctrl_header.free_ctrl_sect].type = SECT_CONT_DATA;
	}

	if(ctrl_header.sect_tbl[ctrl_header.free_ctrl_sect].type != SECT_UNUSED || sect_marks[ctrl_header.free_ctrl_sect] != 0xff) {
		for(int i = 0; i < sect_num; i++) {
			if(ctrl_header.free_sect == 0xfffc && !flash_clean(&ctrl_header, sect_marks))
				return false;
			
			ctrl_header.sect_tbl[ctrl_header.free_sect].type = SECT_UNUSED;
			ctrl_header.free_sect = ctrl_header.sect_tbl[ctrl_header.free_sect].next;
		}
	}

	/* add new ctrl to the general sector table */
	for(int i = 0, s = new_ctrl_sects[0]; i < sect_num; i++) {
		if(i != sect_num - 1) {
			ctrl_header.sect_tbl[s].next = new_ctrl_sects[i + 1];
			s = new_ctrl_sects[i + 1];
		} else {
			ctrl_header.sect_tbl[s].next = 0xfffc;
		}
	}

	/* updating last free sector */
	update_last_free_sect(&ctrl_header);

	/* building ctrl header */
	uint8_t *new_ctrl = build_ctrl_header(&ctrl_header);
	uint8_t *p_ctrl = new_ctrl;

	/* ctrl contents */
	uint32_t crc = crc32(0, NULL, 0);
	for(int i = 0; i < sect_num; i++) {
		bool frag;
		if((i != sect_num - 1) && ((new_ctrl_sects[i] & 0xff) == 0xff))
			frag = true;
		else
			frag = false;

		uint32_t read_count = TFS_PAGE_SIZE - (frag ? 3 : 1);

		struct sect_action new_act = {
			.type = act_types[i],
			.sect_num = new_ctrl_sects[i]
		};
		memset(new_act.sect_data, 0xff, TFS_SECT_SIZE);
		memcpy(new_act.sect_data, p_ctrl, remaining < (TFS_SECT_SIZE) ? remaining : read_count);
		if(frag)
			*(uint16_t *)&new_act.sect_data[509] = new_ctrl_sects[i + 1];

		new_act.sect_data[TFS_SECT_SIZE] = 0xf0;
		if(i + 1 < sect_num)
			crc = crc32(crc, new_act.sect_data, TFS_SECT_SIZE);
		else
			*(uint32_t *)&new_act.sect_data[TFS_PAGE_SIZE - 5] = crc32(crc, new_act.sect_data, TFS_PAGE_SIZE - 5);

		add_sect_action(act_list, &new_act);

		p_ctrl += read_count;
		remaining -= read_count;
	}
	free(new_ctrl);

	free_ctrl_header(&ctrl_header);

	return true;
}

/* reset the parser (reader) */
static void parser_reset() {
	memset(tfs_md5_dig, 0x00, sizeof(tfs_md5_dig));
	tfs_md5_dig_set = false;
	memset(tfs_version, 0x00, sizeof(tfs_version));
	tfs_version_set = false;
	tfs_num_dir = 0;
	tfs_max_files = 0;
	cfg_line = 1;
	cfg_pointer = 0;
	tfs_dir_table = NULL;
	tfs_file_table = NULL;
}

/* read next command [COMMAND] or [KEY : VALUE] */
static bool cfg_next_command(uint8_t *cfg, uint32_t cfg_size, char *key_buf, char *value_buf) {
	CHECK_EOF();

	while(cfg[cfg_pointer] == '#') {
		while(true) {
			CHECK_EOF();
			if(cfg[++cfg_pointer] == '\n') {
				cfg_pointer++;
				cfg_line++;
				break;
			}
		}
	}

	uint8_t c;
	int kv_p;

	kv_p = 0;
	while((c = cfg[cfg_pointer]) != ' ' && c != '\n') {
		CHECK_EOF();
		if(c != '\r') {
			key_buf[kv_p++] = c;
		}
		cfg_pointer++;
	}
	key_buf[kv_p] = '\0';

	if(c == '\n') {
		CHECK_EOF();
		cfg_pointer++;
		cfg_line++;
		return true;
	}

	while((c = cfg[cfg_pointer++]) != ':') {
		CHECK_EOF();
		if(c == '\n') cfg_line++;
	}

	while((c = cfg[cfg_pointer]) == ' ') {
		CHECK_EOF();
		cfg_pointer++;
	}

	kv_p = 0;
	while((c = cfg[cfg_pointer++]) != '\n') {
		CHECK_EOF();
		if(c != '\r')
			value_buf[kv_p++] = c;
	}
	value_buf[kv_p] = '\0';

	cfg_line++;

	return true;
}

/* fix control sector order if fragmentated */
uint8_t *tfs4_ctrl_fix_sect_order(uint8_t *ctrl, uint32_t ctrl_size, uint16_t *sect_nums, uint32_t ctrl_sect_num) {
	uint16_t curr_sect = 0;
	for(int i = 0; i < ctrl_size; i += 511) {
		if(crc32(crc32(0, ctrl + i, ctrl_size - i), ctrl, i) == 0x2144df1c) {
			curr_sect = i / 511;
			break;
		}
	}

	uint8_t *new_ctrl = malloc(ctrl_size);
	if(!new_ctrl) {
		perror("malloc");
		return NULL;
	}
	uint8_t *p_new_ctrl = new_ctrl;
	for(int i = 0; i < ctrl_sect_num; i++) {
		if((sect_nums[curr_sect] & 0xff) == 0xff) {

			memcpy(p_new_ctrl, ctrl + curr_sect * (TFS_SECT_SIZE), TFS_PAGE_SIZE - 3);
			p_new_ctrl += TFS_PAGE_SIZE - 3;
			uint16_t next_sect = *(uint16_t *)(ctrl + curr_sect * (TFS_SECT_SIZE) + (TFS_PAGE_SIZE - 3));

			for(int k = 0; k < ctrl_sect_num; k++) {
				if(sect_nums[k] == next_sect) {
					curr_sect = k;
					break;
				} else if(k == ctrl_sect_num - 1) {
					printf("TFS: cannot find the next control sector\n");
					free(new_ctrl);
					return NULL;
				}
			}

		} else {

			memcpy(p_new_ctrl, ctrl + curr_sect * (TFS_SECT_SIZE), TFS_SECT_SIZE);
			p_new_ctrl += TFS_SECT_SIZE;
			curr_sect++;

		}
	}

	return new_ctrl;
}

/* main TFS patch function */
bool tfs4_patch(uint8_t *tfs, uint8_t *cfg, uint32_t tfs_size, uint32_t cfg_size, uint32_t num_sects, struct sect_action_list *act_list, uint8_t *ctrl, uint8_t *sect_marks, bool update_tfs_version_code) {
	char cfg_key[256];
	char cfg_value[256];

	parser_reset();

	/* parse cfg */
	while(true) {
		if(!cfg_next_command(cfg, cfg_size, cfg_key, cfg_value)) {
			printf("TFS: line %d: unexpected EOF\n", cfg_line);
			return NULL;
		}

		if(!strcmp(cfg_key, "END_TFS")) {

			break;

		} else if(!strcmp(cfg_key, "MD5")) {

			if(strlen(cfg_value) != 32) {
				printf("TFS: MD5 value length incorrect\n");
				return NULL;
			}

			for(int i = 0; i < 16; i++) {
				sscanf(&cfg_value[i << 1], "%02hhx", &tfs_md5_dig[i]);
			}

			tfs_md5_dig_set = true;

		} else if(!strcmp(cfg_key, "TFSVERSION")) {

			strncpy(tfs_version, cfg_value, sizeof(tfs_version));

			tfs_version_set = true;

		} else if(!strcmp(cfg_key, "NUM_DIR")) {

			tfs_num_dir = atoi(cfg_value) + 1;

		} else if(!strcmp(cfg_key, "MAXFILES")) {

			tfs_max_files = atoi(cfg_value);

		} else if(!strcmp(cfg_key, "SET_DIR")) {

			tfs_dir_table = malloc(tfs_num_dir * sizeof(CFG_DIR) + sizeof(CFG_DIR));
			if(!tfs_dir_table) {
				perror("TFS");
				return NULL;
			}

			strcpy(tfs_dir_table[0].dir_name, "/a");

			int i;
			for(i = 1; i < tfs_num_dir; ) {
				if(!cfg_next_command(cfg, cfg_size, cfg_key, cfg_value)) {
					printf("TFS: line %d: unexpected EOF\n", cfg_line);
					if(tfs_dir_table) free(tfs_dir_table);
					return NULL;
				}
				
				if(!strcmp(cfg_key, "DIR_NAME")) {
					strcpy(tfs_dir_table[i].dir_name, "/a");
					strcat(tfs_dir_table[i].dir_name, cfg_value);
					i++;
				} else if(!strcmp(cfg_key, "END_DIR")) {
					break;
				}
			}

			if(i < tfs_num_dir) {
				printf("TFS: directory table incomplete\n");
				free(tfs_dir_table);
				return NULL;
			}

		} else if(!strcmp(cfg_key, "SET_FILE")) {

			tfs_file_table = malloc(tfs_max_files * sizeof(CFG_FILE));
			if(!tfs_file_table) {
				perror("TFS");
				return NULL;
			}

			int i;
			for(i = 0; i < tfs_max_files; ) {
				if(!cfg_next_command(cfg, cfg_size, cfg_key, cfg_value)) {
					printf("TFS: line %d: unexpected EOF\n", cfg_line);
					if(tfs_file_table) free(tfs_file_table);
					return NULL;
				}
				
				if(!strcmp(cfg_key, "FILE_NAME")) {
					strcpy(tfs_file_table[i].file_name, "/a");
					strcat(tfs_file_table[i].file_name, cfg_value);
				} else if(!strcmp(cfg_key, "FILE_SIZE")) {
					tfs_file_table[i++].file_size = atoi(cfg_value);
				} else if(!strcmp(cfg_key, "END_FILE")) {
					break;
				}
			}

			if(i < tfs_max_files) {
				printf("TFS: file table incomplete\n");
				free(tfs_file_table);
				return NULL;
			}

		}
	}

	/* check md5 */
	if(tfs_md5_dig_set) {
		uint8_t calc_dig[EVP_MAX_MD_SIZE];
		uint32_t calc_dig_size;

		EVP_MD_CTX *evp_ctx = EVP_MD_CTX_new();
		const EVP_MD *evp_md = EVP_get_digestbyname("MD5");
		EVP_DigestInit_ex(evp_ctx, evp_md, NULL);
		EVP_DigestUpdate(evp_ctx, tfs, tfs_size);
		EVP_DigestFinal_ex(evp_ctx, calc_dig, &calc_dig_size);
		EVP_MD_CTX_free(evp_ctx);

		if(memcmp(calc_dig, tfs_md5_dig, calc_dig_size)) {
			printf("TFS: MD5 value wrong\n");
			return NULL;
		}
	} else {
		printf("TFS: WARNING! No %s entry in CFG\n", "MD5");
	}

	/* check tfs version */
	if(tfs_version_set) {
		printf("TFS version: %s\n", tfs_version);
		if(strcmp(tfs_version, "TFS4.0_CORONA_01")) {
			printf("TFS: version %s is not supported\n", tfs_version);
			return NULL;
		}
	} else {
		printf("TFS: WARNING! No %s entry in CFG\n", "TFSVERSION");
	}

	bool status = patch_start(tfs_version, tfs, tfs_size, num_sects, act_list, ctrl, sect_marks, update_tfs_version_code);

	free(tfs_dir_table);
	free(tfs_file_table);

	return status;
}
