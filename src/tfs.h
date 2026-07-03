#include <stdint.h>

#define TFS_SECT_SIZE		511
#define TFS_PAGE_SIZE		512

#define DEFAULT_CTRL_SECT	256
#define DEFAULT_DATA_SECT	512

#define TFS_SECTS			0x7800

/* info about what to do with the sectors */

typedef enum {
	SECT_ACTION_WRITE,
	SECT_ACTION_ERASE_AND_WRITE,
	SECT_ACTION_MARK
} SECT_ACTION_TYPE;

struct sect_action {
	SECT_ACTION_TYPE type;
	uint16_t sect_num;
	uint8_t sect_data[512];
};

struct sect_action_list {
	uint32_t num_acts;
	struct sect_action *acts;
};

extern uint8_t *tfs4_ctrl_fix_sect_order(uint8_t *ctrl, uint32_t ctrl_size, uint16_t *sect_nums, uint32_t ctrl_sect_num);

extern bool tfs4_patch(uint8_t *tfs, uint8_t *cfg, uint32_t tfs_size, uint32_t cfg_size, uint32_t num_sects, struct sect_action_list *act_list, uint8_t *ctrl, uint8_t *sect_marks, bool update_tfs_version_code);
