#ifndef _CTRLSYM_H
#define _CTRLSYM_H 1

enum {
	CTRL_NUL, /* Null                      (0x00, \0) */
	CTRL_SOH, /* Start of Heading          (0x01) */
	CTRL_STX, /* Start of Text             (0x02) */
	CTRL_ETX, /* End of Text               (0x03) */
	CTRL_EOT, /* End of Transmission       (0x04) */
	CTRL_ENQ, /* Enquiry                   (0x05) */
	CTRL_ACK, /* Acknowledgement           (0x06) */
	CTRL_BEL, /* Bell                      (0x07, \a) */
	CTRL_BS,  /* Backspace                 (0x08, \b) */
	CTRL_HT,  /* Horizontal Tab            (0x09, \t) */
	CTRL_LF,  /* Line Feed                 (0x0a, \n) */
	CTRL_VT,  /* Vertical Tab              (0x0b, \v) */
	CTRL_FF,  /* Form Feed                 (0x0c, \f) */
	CTRL_CR,  /* Carriage Return           (0x0d, \r) */
	CTRL_SO,  /* Shift Out                 (0x0e) */
	CTRL_SI,  /* Shift In                  (0x0f) */
	CTRL_DLE, /* Data Link Escape          (0x10) */
	CTRL_DC1, /* Device Control 1          (0x11) */
	CTRL_DC2, /* Device Control 2          (0x12) */
	CTRL_DC3, /* Device Control 3          (0x13) */
	CTRL_DC4, /* Device Control 4          (0x14) */
	CTRL_NAK, /* Negative Acknowledgment   (0x15) */
	CTRL_SYN, /* Synchronous Idle          (0x16) */
	CTRL_ETB, /* End of Transmission Block (0x17) */
	CTRL_CAN, /* Cancel                    (0x18) */
	CTRL_EM,  /* End of Medium             (0x19) */
	CTRL_SUB, /* Substitute                (0x1a) */
	CTRL_ESC, /* Escape                    (0x1b, \e) */
	CTRL_FS,  /* File Separator            (0x1c) */
	CTRL_GS,  /* Group Separator           (0x1d) */
	CTRL_RS,  /* Record Separator          (0x1e) */
	CTRL_US,  /* Unit Separator            (0x1f) */
};

#endif /* _CTRLSYM_H */
