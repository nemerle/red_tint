/*
** load.c - mruby binary loader
**
** See Copyright Notice in mruby.h
*/

#include <limits.h>
#include <stdlib.h>
#include <cassert>
#include <string.h>
#include "mrbconf.h"
#include "mruby/dump.h"
#include "mruby/irep.h"
#include "mruby/compile.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/debug.h"
#include "mruby/error.h"

#if !defined(_WIN32) && SIZE_MAX < UINT32_MAX
# define SIZE_ERROR_MUL(x, y) ((x) > SIZE_MAX / (y))
# define SIZE_ERROR(x) ((x) > SIZE_MAX)
#else
# define SIZE_ERROR_MUL(x, y) (0)
# define SIZE_ERROR(x) (0)
#endif
#ifndef _WIN32
# if SIZE_MAX < UINT32_MAX
#  error "It can't be run this code on this environment (SIZE_MAX < UINT32_MAX)"
# endif
#endif

#if CHAR_BIT != 8
# error This code assumes CHAR_BIT == 8
#endif
static size_t offset_crc_body(void)
{
    //struct rite_binary_header header;
    //return ((uint8_t *)header.binary_crc - (uint8_t *)&header) + sizeof(header.binary_crc);
    return offsetof(rite_binary_header,binary_crc) + sizeof(rite_binary_header::binary_crc);
}
static mrb_irep*
read_irep_record_1(mrb_state *mrb, const uint8_t *bin, uint32_t *len,bool _alloc)
{
    size_t i;
    const uint8_t *src = bin;
    uint16_t tt, pool_data_len, snl;
    size_t plen;
    int ai = mrb->gc().arena_save();
    mrb_irep *irep = mrb_add_irep(mrb);

    // skip record size
    src += sizeof(uint32_t);

    // number of local variable
    irep->nlocals = bin_to_uint16(src);
    src += sizeof(uint16_t);

    // number of register variable
    irep->nregs = bin_to_uint16(src);
    src += sizeof(uint16_t);

    // number of child irep
    irep->rlen = bin_to_uint16_adv(src);


    // Binary Data Section
    // ISEQ BLOCK
    irep->ilen = bin_to_uint32(src);
    src += sizeof(uint32_t);
    if (irep->ilen > 0) {
        if (SIZE_ERROR_MUL(sizeof(mrb_code), irep->ilen))
            return nullptr;
        irep->iseq = (mrb_code *)mrb->gc()._malloc(sizeof(mrb_code) * irep->ilen);

        if (irep->iseq == NULL) {
            return nullptr;
        }
        for (i = 0; i < irep->ilen; i++) {
            irep->iseq[i] = bin_to_uint32_adv(src);     //iseq
        }
    }

    //POOL BLOCK
    plen = bin_to_uint32(src); /* number of pool */
    src += sizeof(uint32_t);
    if (plen > 0) {
        if (SIZE_ERROR_MUL(sizeof(mrb_value), plen)) {
            return nullptr;
        }
        irep->pool = (mrb_value *)mrb->gc()._malloc(sizeof(mrb_value)*plen);
        if (irep->pool == nullptr) {
            return nullptr;
        }

        for (i = 0; i < plen; i++) {
            RString *s;
            tt = *src++; //pool TT
            pool_data_len = bin_to_uint16(src); //pool data length
            src += sizeof(uint16_t);
            if (_alloc) {
                s = RString::create(mrb, (char *)src, pool_data_len);
            }
            else {
                s = RString::create_static(mrb, (char *)src, pool_data_len);
            }
            src += pool_data_len;

            switch (tt) { //pool data
                case IREP_TT_FIXNUM:
                    irep->pool[i] = mrb_value::wrap(s->mrb_str_to_inum(10, false));
                    break;

                case IREP_TT_FLOAT:
                    irep->pool[i] = mrb_float_value(mrb_str_to_dbl(mrb, s, false));
                    break;
                case IREP_TT_STRING:
                    irep->pool[i] = mrb_str_pool(mrb, s);
                    break;
                default:
                    assert(false);
                    irep->pool[i] = mrb_value::nil();
                    /* should not happen */
                    break;
            }
            irep->plen++;
            mrb->gc().arena_restore(ai);
        }
    }

    //SYMS BLOCK
    irep->slen = bin_to_uint32(src);  //syms length
    src += sizeof(uint32_t);
    if (irep->slen > 0) {
        if (SIZE_ERROR_MUL(sizeof(mrb_sym), irep->slen)) {
            return nullptr;
        }
        irep->syms = (mrb_sym *)mrb->gc()._malloc(sizeof(mrb_sym) * irep->slen);
        if (irep->syms == NULL) {
            return nullptr;
        }

        for (i = 0; i < irep->slen; i++) {
            snl = bin_to_uint16(src);               //symbol name length
            src += sizeof(uint16_t);

            if (snl == MRB_DUMP_NULL_SYM_LEN) {
                irep->syms[i] = 0;
                continue;
            }

            if (_alloc) {
                irep->syms[i] = mrb_intern(mrb, (char *)src, snl);
            }
            else {
                irep->syms[i] = mrb_intern_static(mrb, (char *)src, snl);
            }
            src += snl + 1;

            mrb->gc().arena_restore(ai);
        }
    }

    irep->reps = (mrb_irep**)mrb->gc()._malloc(sizeof(mrb_irep*)*irep->rlen);
    *len = src - bin;

    return irep;
}

static mrb_irep*
read_irep_record(mrb_state *mrb, const uint8_t *bin, uint32_t *len,bool _alloc)
{
    mrb_irep *irep = read_irep_record_1(mrb, bin, len,_alloc);
    size_t i;

    bin += *len;
    for (i=0; i<irep->rlen; i++) {
        uint32_t rlen;

        irep->reps[i] = read_irep_record(mrb, bin, &rlen,_alloc);
        bin += rlen;
        *len += rlen;
    }
    return irep;
}

static mrb_irep*
read_section_irep(mrb_state *mrb, const uint8_t *bin,bool _alloc)
{
    uint32_t len;

    bin += sizeof(struct rite_section_irep_header);
    return read_irep_record(mrb, bin, &len,_alloc);
}


static int
read_lineno_record_1(mrb_state *mrb, const uint8_t *bin, mrb_irep *irep, uint32_t *len)
{
    size_t i, fname_len, niseq;
    char *fname;
    uint16_t *lines;
    int ret = MRB_DUMP_OK;

    *len = 0;
    bin += sizeof(uint32_t); // record size
    *len += sizeof(uint32_t);
    fname_len = bin_to_uint16(bin);
    bin += sizeof(uint16_t);
    *len += sizeof(uint16_t);
    if (SIZE_ERROR(fname_len + 1)) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    fname = (char *)mrb->gc()._malloc(fname_len + 1);
    if (fname == NULL) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    memcpy(fname, bin, fname_len);
    fname[fname_len] = '\0';
    bin += fname_len;
    *len += fname_len;

    niseq = bin_to_uint32(bin);
    bin += sizeof(uint32_t); // niseq
    *len += sizeof(uint32_t);
    if (SIZE_ERROR_MUL(niseq, sizeof(uint16_t))) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    lines = (uint16_t *)mrb->gc()._malloc(niseq * sizeof(uint16_t));
    if (lines == nullptr) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    for (i = 0; i < niseq; i++) {
        lines[i] = bin_to_uint16(bin);
        bin += sizeof(uint16_t); // niseq
        *len += sizeof(uint16_t);
    }
    irep->filename = fname;
    irep->lines = lines;
    return ret;
}
static int
read_lineno_record(mrb_state *mrb, const uint8_t *bin, mrb_irep *irep, uint32_t *lenp)
{
    int result = read_lineno_record_1(mrb, bin, irep, lenp);
    size_t i;

    if (result != MRB_DUMP_OK) return result;
    for (i = 0; i < irep->rlen; i++) {
        uint32_t len;

        result = read_lineno_record(mrb, bin, irep->reps[i], &len);
        if (result != MRB_DUMP_OK) break;
        bin += len;
        *lenp += len;
    }
    return result;
}

static int
read_section_lineno(mrb_state *mrb, const uint8_t *bin, mrb_irep *irep)
{
    uint32_t len;

    len = 0;
    bin += sizeof(struct rite_section_lineno_header);

    //Read Binary Data Section
    return read_lineno_record(mrb, bin, irep, &len);
}

static int read_debug_record(mrb_state *mrb, const uint8_t *start, mrb_irep* irep, uint32_t *len, const mrb_sym *filenames, size_t filenames_len) {

    uint8_t const* bin = start;

    if(irep->debug_info)
        return MRB_DUMP_INVALID_IREP;

    irep->debug_info = mrb->gc().new_t<mrb_irep_debug_info>();
    irep->debug_info->pc_count = irep->ilen;

    size_t record_size = bin_to_uint32(bin);
    bin += sizeof(uint32_t);

    irep->debug_info->flen = bin_to_uint16(bin);
    irep->debug_info->files = mrb->gc().new_ta<mrb_irep_debug_info_file*>(irep->debug_info->flen);
    bin += sizeof(uint16_t);

    for (uint16_t f_idx = 0; f_idx < irep->debug_info->flen; ++f_idx) {
        mrb_irep_debug_info_file* const file = mrb->gc().new_t<mrb_irep_debug_info_file>();
        irep->debug_info->files[f_idx] = file;

        file->start_pos = bin_to_uint32_adv(bin);

        // filename
        uint16_t const filename_idx = bin_to_uint16_adv(bin);
        mrb_assert(filename_idx < filenames_len);
        file->filename_sym = filenames[filename_idx];
        size_t len = 0;
        file->filename = mrb_sym2name_len(mrb, file->filename_sym, len);

        file->line_entry_count = bin_to_uint32_adv(bin);
        file->line_type = (mrb_debug_line_type)bin_to_uint8_adv(bin);
        switch(file->line_type) {
            case mrb_debug_line_ary:
                file->lines.ary = mrb->gc().new_ta<uint16_t>(file->line_entry_count);
                for(size_t l = 0; l < file->line_entry_count; ++l) {
                    file->lines.ary[l] = bin_to_uint16_adv(bin);
                }
                break;

            case mrb_debug_line_flat_map:
                file->lines.flat_map = mrb->gc().new_ta<mrb_irep_debug_info_line>(file->line_entry_count);
                for(size_t l = 0; l < file->line_entry_count; ++l) {
                    file->lines.flat_map[l].start_pos = bin_to_uint32_adv(bin);
                    file->lines.flat_map[l].line = bin_to_uint16_adv(bin);
                }
                break;

            default: return MRB_DUMP_GENERAL_FAILURE;
        }
    }

    if((long)record_size != (bin - start)) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    for (size_t i = 0; i < irep->rlen; i++) {
        uint32_t len;
        int ret;

        ret =read_debug_record(mrb, bin, irep->reps[i], &len, filenames, filenames_len);
        if (ret != MRB_DUMP_OK) return ret;
        bin += len;
    }

    *len = bin - start;

    return MRB_DUMP_OK;
}

static int
read_section_debug(mrb_state *mrb, const uint8_t *start, mrb_irep *irep,bool _alloc)
{
    uint32_t len = 0;
    int result;

    uint8_t const* bin = start;
    rite_section_debug_header const* header = (struct rite_section_debug_header const*)bin;
    bin += sizeof(struct rite_section_debug_header);



    size_t filenames_len = bin_to_uint16_adv(bin);
    mrb_sym* filenames = (mrb_sym*)mrb->gc()._malloc(sizeof(mrb_sym*) * filenames_len);

    for(uint16_t i = 0; i < filenames_len; ++i) {
        uint16_t f_len = bin_to_uint16_adv(bin);
        if (_alloc) {
            filenames[i] = mrb_intern(mrb, (const char *)bin, f_len);
        }
        else {
            filenames[i] = mrb_intern_static(mrb, (const char *)bin, f_len);
        }
        bin += f_len;
    }
    result = read_debug_record(mrb, bin, irep, &len, filenames, filenames_len);
    if (result != MRB_DUMP_OK)
        goto debug_exit;
    bin+=len;
    if ((bin - start) != bin_to_uint32(header->section_size)) {
        result = MRB_DUMP_GENERAL_FAILURE;
    }

debug_exit:
    mrb->gc()._free(filenames);
    return result;
}

static int
read_binary_header(const uint8_t *bin, size_t *bin_size, uint16_t *crc)
{
    const struct rite_binary_header *header = (const struct rite_binary_header *)bin;

    if(memcmp(header->binary_identify, RITE_BINARY_IDENTIFIER, sizeof(header->binary_identify)) != 0) {
        return MRB_DUMP_INVALID_FILE_HEADER;
    }

    if(memcmp(header->binary_version, RITE_BINARY_FORMAT_VER, sizeof(header->binary_version)) != 0) {
        return MRB_DUMP_INVALID_FILE_HEADER;
    }

    *crc = bin_to_uint16(header->binary_crc);
    if (bin_size) {
        *bin_size = bin_to_uint32(header->binary_size);
    }

    return MRB_DUMP_OK;
}

mrb_irep *
mrb_read_irep(mrb_state *mrb, const uint8_t *bin)
{
    int result;
    mrb_irep *irep = nullptr;
    const struct rite_section_header *section_header;
    uint16_t crc;
    size_t bin_size = 0;
    size_t n;

    if ((mrb == NULL) || (bin == NULL)) {
        return nullptr;
    }

    result = read_binary_header(bin, &bin_size, &crc);
    if(result != MRB_DUMP_OK) {
        return nullptr;
    }

    n = offset_crc_body();
    if(crc != calc_crc_16_ccitt(bin + n, bin_size - n, 0)) {
        return nullptr;
    }

    bin += sizeof(rite_binary_header);

    do {
        section_header = (const rite_section_header *)bin;
        if(memcmp(section_header->section_identify, RITE_SECTION_IREP_IDENTIFIER, sizeof(section_header->section_identify)) == 0) {
            irep = read_section_irep(mrb, bin, false);
            if (!irep) return NULL;
        }
        else if(memcmp(section_header->section_identify, RITE_SECTION_LINENO_IDENTIFIER, sizeof(section_header->section_identify)) == 0) {
            if (!irep) return NULL;   /* corrupted data */
            result = read_section_lineno(mrb, bin, irep);
            if (result < MRB_DUMP_OK) {
                return NULL;
            }
        }
        else if (memcmp(section_header->section_identify, RITE_SECTION_DEBUG_IDENTIFIER, sizeof(section_header->section_identify)) == 0) {
            if (!irep) return NULL;   /* corrupted data */
            result = read_section_debug(mrb, bin, irep,false);
            if (result < MRB_DUMP_OK) {
                return NULL;
            }
        }
        bin += bin_to_uint32(section_header->section_size);
    } while(memcmp(section_header->section_identify, RITE_BINARY_EOF, sizeof(section_header->section_identify)) != 0);

    return irep;
}

static void
irep_error(mrb_state *mrb)
{
    static const char msg[] = "irep load error";
    mrb->m_exc = mrb_exc_new(E_SCRIPT_ERROR, msg, sizeof(msg) - 1).object_ptr();
}

mrb_value
mrb_load_irep_ctx(mrb_state *mrb, const uint8_t *bin, mrbc_context *c)
{
    mrb_irep *irep = mrb_read_irep(mrb, bin);

    if (!irep) {
        irep_error(mrb);
        return mrb_value::nil();
    }
    auto prc = RProc::create(mrb, irep);
    mrb_irep_decref(mrb->gc(), irep);
    if (c && c->no_exec) return mrb_value::wrap(prc);
    return mrb->mrb_context_run(prc, mrb_top_self(mrb), 0);
}
mrb_value
mrb_load_irep(mrb_state *mrb, const uint8_t *bin)
{
    return mrb_load_irep_ctx(mrb, bin, nullptr);
}
#ifdef ENABLE_STDIO
static int
read_lineno_record_file(mrb_state *mrb, FILE *fp, mrb_irep *irep)
{
    const size_t record_header_size = 4;
    uint8_t header[record_header_size];
    int result;
    size_t i, buf_size;
    uint32_t len;
    void *ptr;
    uint8_t *buf;

    if (fread(header, record_header_size, 1, fp) == 0) {
        return MRB_DUMP_READ_FAULT;
    }
    buf_size = bin_to_uint32(&header[0]);
    if (SIZE_ERROR(buf_size)) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    ptr = mrb->gc()._malloc(buf_size);
    if (!ptr) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    buf = (uint8_t *)ptr;

    if (fread(&buf[record_header_size], buf_size - record_header_size, 1, fp) == 0) {
        return MRB_DUMP_READ_FAULT;
    }
    result = read_lineno_record_1(mrb, buf, irep, &len);
    mrb->gc()._free(ptr);

    if (result != MRB_DUMP_OK)
        return result;
    for (i = 0; i < irep->rlen; i++) {
        result = read_lineno_record_file(mrb, fp, irep->reps[i]);
        if (result != MRB_DUMP_OK)
            break;
    }
    return result;
}
static int32_t
read_section_lineno_file(mrb_state *mrb, FILE *fp, mrb_irep *irep)
{
    struct rite_section_lineno_header header;

    if (fread(&header, sizeof(struct rite_section_lineno_header), 1, fp) == 0) {
        return MRB_DUMP_READ_FAULT;
    }
    //Read Binary Data Section
    return read_lineno_record_file(mrb, fp, irep);
}
static mrb_irep*
read_irep_record_file(mrb_state *mrb, FILE *fp)
{
    const size_t record_header_size = 1 + 4;
    uint8_t header[record_header_size];
    size_t buf_size, i;
    uint32_t len;
    mrb_irep *irep = NULL;
    void *ptr;
    uint8_t *buf;

    if (fread(header, record_header_size, 1, fp) == 0) {
        return NULL;
    }
    buf_size = bin_to_uint32(&header[0]);
    if (SIZE_ERROR(buf_size)) {
        return NULL;
    }
    ptr = mrb->gc()._malloc(buf_size);
    if (!ptr)
        return NULL;
    buf = (uint8_t *)ptr;
    memcpy(buf,header,record_header_size);
    if (fread(&buf[record_header_size], buf_size - record_header_size, 1, fp) == 0) {
        return NULL;
    }
    irep = read_irep_record_1(mrb, buf, &len,true);
    mrb->gc()._free(ptr);
    if (!irep) return NULL;
    for (i=0; i<irep->rlen; i++) {
        irep->reps[i] = read_irep_record_file(mrb, fp);
        if (!irep->reps[i]) return NULL;
    }
    return irep;
}
static mrb_irep*
read_section_irep_file(mrb_state *mrb, FILE *fp)
{
    struct rite_section_irep_header header;

    if (fread(&header, sizeof(struct rite_section_irep_header), 1, fp) == 0) {
        return NULL;
    }
    return read_irep_record_file(mrb, fp);
}

mrb_irep*
mrb_read_irep_file(mrb_state *mrb, FILE* fp)
{
    mrb_irep *irep=nullptr;
    int result;
    uint8_t *buf;
    uint16_t crc, crcwk = 0;
    uint32_t section_size = 0;
    size_t nbytes;
    struct rite_section_header section_header;
    long fpos;
    size_t block_size = 1 << 14;
    const uint8_t block_fallback_count = 4;
    int i;
    const size_t buf_size = sizeof(struct rite_binary_header);

    if ((mrb == NULL) || (fp == NULL)) {
        return NULL;
    }

    /* You don't need use SIZE_ERROR as buf_size is enough small. */
    buf = (uint8_t *)mrb->gc()._malloc(buf_size);
    if (!buf) {
        return NULL;
    }
    if (fread(buf, buf_size, 1, fp) == 0) {
        mrb->gc()._free(buf);
        return NULL;
    }
    result = read_binary_header(buf, NULL, &crc);
    mrb->gc()._free(buf);
    if (result != MRB_DUMP_OK) {
        return NULL;
    }

    /* verify CRC */
    fpos = ftell(fp);
    /* You don't need use SIZE_ERROR as block_size is enough small. */
    for (i = 0; i < block_fallback_count; i++,block_size >>= 1){
        buf = (uint8_t *)mrb->gc().mrb_malloc_simple(block_size);
        if (buf) break;
    }
    if (!buf) {
        return NULL;
    }
    fseek(fp, offset_crc_body(), SEEK_SET);
    while ((nbytes = fread(buf, 1, block_size, fp)) > 0) {
        crcwk = calc_crc_16_ccitt(buf, nbytes, crcwk);
    }
    mrb->gc()._free(buf);
    if (nbytes == 0 && ferror(fp)) {
        return NULL;
    }
    if (crcwk != crc) {
        return NULL;
    }
    fseek(fp, fpos + section_size, SEEK_SET);

    // read sections
    do {
        fpos = ftell(fp);
        if (fread(&section_header, sizeof(struct rite_section_header), 1, fp) == 0) {
            return NULL;
        }
        section_size = bin_to_uint32(section_header.section_size);

        if (memcmp(section_header.section_identify, RITE_SECTION_IREP_IDENTIFIER, sizeof(section_header.section_identify)) == 0) {
            fseek(fp, fpos, SEEK_SET);
            irep = read_section_irep_file(mrb, fp);
            if (!irep) return NULL;
        }
        else if (memcmp(section_header.section_identify, RITE_SECTION_LINENO_IDENTIFIER, sizeof(section_header.section_identify)) == 0) {
            if (!irep) return NULL;   /* corrupted data */
            fseek(fp, fpos, SEEK_SET);
            result = read_section_lineno_file(mrb, fp, irep);
            if (result < MRB_DUMP_OK) return NULL;
        }
        else if (memcmp(section_header.section_identify, RITE_SECTION_DEBUG_IDENTIFIER, sizeof(section_header.section_identify)) == 0) {
            if (!irep) return NULL;   /* corrupted data */
            else {
                uint8_t* const bin = (uint8_t*)mrb->gc()._malloc(section_size);

                fseek(fp, fpos, SEEK_SET);
                if(fread((char*)bin, section_size, 1, fp) != 1) {
                    mrb->gc()._free(bin);
                    return NULL;
                }
                result = read_section_debug(mrb, bin, irep,true);
                mrb->gc()._free(bin);
            }
            if (result < MRB_DUMP_OK) return NULL;
        }

        fseek(fp, fpos + section_size, SEEK_SET);
    } while (memcmp(section_header.section_identify, RITE_BINARY_EOF, sizeof(section_header.section_identify)) != 0);

    return irep;
}

mrb_value mrb_load_irep_file_cxt(mrb_state *mrb, FILE* fp, mrbc_context *c)
{
    mrb_irep *irep = mrb_read_irep_file(mrb, fp);

    if (!irep) {
        irep_error(mrb);
        return mrb_value::nil();
    }
    auto proc = RProc::create(mrb, irep);
    mrb_irep_decref(mrb->gc(), irep);
    if (c && c->no_exec) return mrb_value::wrap(proc);
    return mrb->mrb_context_run(proc, mrb_top_self(mrb), 0);
}
mrb_value
mrb_load_irep_file(mrb_state *mrb, FILE* fp)
{
    return mrb_load_irep_file_cxt(mrb, fp, NULL);
}
#endif /* ENABLE_STDIO */
