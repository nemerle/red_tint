/*
** mruby/dump.h - mruby binary dumper (mrbc binary format)
**
** See Copyright Notice in mruby.h
*/

#pragma once

#include "mruby.h"
uint16_t
calc_crc_16_ccitt(const uint8_t *src, size_t nbytes, uint16_t crcwk);
#ifdef ENABLE_STDIO
int mrb_dump_irep_binary(mrb_state*, mrb_irep*, int, FILE*);
int mrb_dump_irep_cfunc(mrb_state *mrb, mrb_irep *irep, int debug_info, FILE *fp, const char *initname);
mrb_irep* mrb_read_irep_file(mrb_state*, FILE*);
#endif
mrb_irep *mrb_read_irep(mrb_state*, const uint8_t*);

mrb_value mrb_load_irep_file_cxt(mrb_state*, FILE*, struct mrbc_context*);
#ifdef ENABLE_STDIO
mrb_value mrb_load_irep_file(mrb_state*,FILE*);
#endif

/* dump/load error code
 *
 * NOTE: MRB_DUMP_GENERAL_FAILURE is caused by
 * unspecified issues like malloc failed.
 */
#define MRB_DUMP_OK                   0
#define MRB_DUMP_GENERAL_FAILURE      (-1)
#define MRB_DUMP_WRITE_FAULT          (-2)
#define MRB_DUMP_READ_FAULT           (-3)
#define MRB_DUMP_CRC_ERROR            (-4)
#define MRB_DUMP_INVALID_FILE_HEADER  (-5)
#define MRB_DUMP_INVALID_IREP         (-6)
#define MRB_DUMP_INVALID_ARGUMENT     (-7)

/* null symbol length */
#define MRB_DUMP_NULL_SYM_LEN         0xFFFF

/* Rite Binary File header */
#define RITE_BINARY_IDENTIFIER        "RITE"
#define RITE_BINARY_FORMAT_VER        "0002"
#define RITE_COMPILER_NAME            "MATZ"
#define RITE_COMPILER_VERSION         "0000"

#define RITE_VM_VER                   "0000"

#define RITE_BINARY_EOF               "END\0"
#define RITE_SECTION_IREP_IDENTIFIER  "IREP"
#define RITE_SECTION_LINENO_IDENTIFIER "LINE"
#define RITE_SECTION_DEBUG_IDENTIFIER  "DBG\0"

#define MRB_DUMP_DEFAULT_STR_LEN      128

// binary header
struct rite_binary_header {
    uint8_t binary_identify[4]; // Binary Identifier
    uint8_t binary_version[4];  // Binary Format Version
    uint8_t binary_crc[2];      // Binary CRC
    uint8_t binary_size[4];     // Binary Size
    uint8_t compiler_name[4];   // Compiler name
    uint8_t compiler_version[4];
};

// section header
struct rite_section_header {
    uint8_t section_identify[4];
    uint8_t section_size[4];
};

struct rite_section_irep_header : public rite_section_header {

    uint8_t rite_version[4];    // Rite Instruction Specification Version
};

struct rite_section_lineno_header : public rite_section_header {

};
struct rite_section_debug_header : public rite_section_header {
};
struct rite_binary_footer : public rite_section_header {
};

static inline int
uint8_to_bin(uint8_t s, uint8_t *bin)
{
    *bin = s;
    return sizeof(uint8_t);
}

static inline int
uint16_to_bin(uint16_t s, uint8_t *bin)
{
    *bin++ = (s >> 8) & 0xff;
    *bin   = s & 0xff;
    return sizeof(uint16_t);
}

static inline int
uint32_to_bin(uint32_t l, uint8_t *bin)
{
    *bin++ = (l >> 24) & 0xff;
    *bin++ = (l >> 16) & 0xff;
    *bin++ = (l >> 8) & 0xff;
    *bin   = l & 0xff;
    return sizeof(uint32_t);
}

static inline uint32_t
bin_to_uint32(const uint8_t *bin)
{
    return (uint32_t)bin[0] << 24 |
            (uint32_t)bin[1] << 16 |
            (uint32_t)bin[2] << 8  |
            (uint32_t)bin[3];
}
static inline uint32_t
bin_to_uint32_adv(const uint8_t *&bin)
{
    uint32_t res= (uint32_t)bin[0] << 24 |
            (uint32_t)bin[1] << 16 |
            (uint32_t)bin[2] << 8  |
            (uint32_t)bin[3];
    bin += sizeof(uint32_t);
    return res;
}

static inline uint16_t
bin_to_uint16(const uint8_t *bin)
{
    return (uint16_t)bin[0] << 8 |
            (uint16_t)bin[1];
}
static inline uint16_t
bin_to_uint16_adv(const uint8_t *&bin)
{
    uint16_t res=(uint16_t)bin[0] << 8 |
            (uint16_t)bin[1];
    bin+=sizeof(uint16_t);
    return res;
}

static inline uint8_t
bin_to_uint8(const uint8_t *bin)
{
    return (uint8_t)bin[0];
}
static inline uint8_t
bin_to_uint8_adv(const uint8_t *&bin)
{
    uint8_t res=(uint8_t)bin[0];
    bin+=sizeof(uint8_t);
    return res;
}

/* crc.c */
