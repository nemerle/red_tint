/*
** mruby/debug.h - mruby debug info
**
** See Copyright Notice in mruby.h
*/

#pragma once
#include <stdint.h>

struct mrb_irep;
struct mrb_state;
enum mrb_debug_line_type {
  mrb_debug_line_ary = 0,
  mrb_debug_line_flat_map = 1
};

struct mrb_irep_debug_info_line {
  uint32_t start_pos;
  uint16_t line;
};

struct mrb_irep_debug_info_file {
  uint32_t start_pos;
  const char *filename;
  mrb_sym filename_sym;
  uint32_t line_entry_count;
  mrb_debug_line_type line_type;
  union {
    void *ptr;
    mrb_irep_debug_info_line *flat_map;
    uint16_t *ary;
  } lines;
};

struct mrb_irep_debug_info {
  uint32_t pc_count;
  uint16_t flen;
  mrb_irep_debug_info_file **files;
};

/*
 * get line from irep's debug info and program counter
 * @return returns NULL if not found
 */
const char *mrb_debug_get_filename(mrb_irep *irep, uint32_t pc);

/*
 * get line from irep's debug info and program counter
 * @return returns -1 if not found
 */
int32_t mrb_debug_get_line(mrb_irep *irep, uint32_t pc);

mrb_irep_debug_info_file *mrb_debug_info_append_file(
    mrb_state *mrb, mrb_irep *irep,
    uint32_t start_pos, uint32_t end_pos);
mrb_irep_debug_info *mrb_debug_info_alloc(mrb_state *mrb, mrb_irep *irep);
void mrb_debug_info_free(mrb_state *mrb, mrb_irep_debug_info *d);
