/*
** load.c - mruby binary loader
**
** See Copyright Notice in mruby.h
*/

#ifndef SIZE_MAX
/* Some versions of VC++
  * has SIZE_MAX in stdint.h
  */
# include <limits.h>
#endif
#include <stdlib.h>
#include <cassert>
#include <string.h>
#include "mrbconf.h"
#include "mruby/dump.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/string.h"

#ifndef _WIN32
# if SIZE_MAX < UINT32_MAX
#  error "It can't be run this code on this environment (SIZE_MAX < UINT32_MAX)"
# endif
#endif

#if CHAR_BIT != 8
# error This code assumes CHAR_BIT == 8
#endif
static void irep_free(size_t sirep, mrb_state *mrb)
{
    size_t i;
    void *p;

    for (i = sirep; i < mrb->irep_len; i++) {
        if (mrb->m_irep[i]) {
            p = mrb->m_irep[i]->iseq;
            if (p)
                mrb->m_gc._free(p);

            p = mrb->m_irep[i]->m_pool;
            if (p)
                mrb->m_gc._free(p);

            p = mrb->m_irep[i]->syms;
            if (p)
                mrb->m_gc._free(p);

            mrb->m_gc._free(mrb->m_irep[i]);
        }
    }
}
static size_t offset_crc_body()
{
    //struct rite_binary_header header;
    //return ((uint8_t *)header.binary_crc - (uint8_t *)&header) + sizeof(header.binary_crc);
    return offsetof(rite_binary_header,binary_crc) + sizeof(rite_binary_header::binary_crc);
}

static int read_rite_irep_record(mrb_state *mrb, const uint8_t *bin,size_t , uint32_t *len)
{
    int ret;
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

    // Binary Data Section
    // ISEQ BLOCK
    irep->ilen = bin_to_uint32(src);
    src += sizeof(uint32_t);
    if (irep->ilen > 0) {
        irep->iseq = (mrb_code *)mrb->gc()._malloc(sizeof(mrb_code) * irep->ilen);
        if (irep->iseq == NULL) {
            ret = MRB_DUMP_GENERAL_FAILURE;
            goto error_exit;
        }
        for (i = 0; i < irep->ilen; i++) {
            irep->iseq[i] = bin_to_uint32(src);     //iseq
            src += sizeof(uint32_t);
        }
    }

    //POOL BLOCK
    plen = bin_to_uint32(src); /* number of pool */
    src += sizeof(uint32_t);
    if (plen > 0) {
        irep->m_pool = (mrb_value *)mrb->gc()._malloc(sizeof(mrb_value) * plen);
        if (irep->m_pool == NULL) {
            ret = MRB_DUMP_GENERAL_FAILURE;
            goto error_exit;
        }

        for (i = 0; i < plen; i++) {
            mrb_value s;
            tt = *src++; //pool TT
            pool_data_len = bin_to_uint16(src); //pool data length
            src += sizeof(uint16_t);
            s = mrb_str_new(mrb, (char *)src, pool_data_len);
            src += pool_data_len;
            switch (tt) { //pool data
            case MRB_TT_FIXNUM:
                irep->m_pool[i] = mrb_str_to_inum(mrb, s, 10, false);
                break;

            case MRB_TT_FLOAT:
                irep->m_pool[i] = mrb_float_value(mrb_str_to_dbl(mrb, s, false));
                break;

            case MRB_TT_STRING:
                irep->m_pool[i] = s;
                break;

            default:
                irep->m_pool[i] = mrb_nil_value();
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
        irep->syms = (mrb_sym *)mrb->gc()._malloc(sizeof(mrb_sym) * irep->slen);
        if (irep->syms == NULL) {
            ret = MRB_DUMP_GENERAL_FAILURE;
            goto error_exit;
        }

        for (i = 0; i < irep->slen; i++) {
            snl = bin_to_uint16(src);               //symbol name length
            src += sizeof(uint16_t);

            if (snl == MRB_DUMP_NULL_SYM_LEN) {
                irep->syms[i] = 0;
                continue;
            }

            irep->syms[i] = mrb_intern2(mrb, (char *)src, snl);
            src += snl + 1;

            mrb->gc().arena_restore(ai);
        }
    }
    *len = src - bin;

    ret = MRB_DUMP_OK;
error_exit:
    return ret;
}

static int
read_rite_section_irep(mrb_state *mrb, const uint8_t *bin)
{
    int result;
    size_t sirep;

    uint32_t len;
    uint16_t nirep;
    uint16_t n;
    const struct rite_section_irep_header *header;

    header = (const struct rite_section_irep_header*)bin;
    bin += sizeof(struct rite_section_irep_header);

    sirep = mrb->irep_len;
    nirep = bin_to_uint16(header->nirep);

    //Read Binary Data Section
    for (n = 0; n < nirep; n++) {
        result = read_rite_irep_record(mrb, bin, 0, &len);
        if (result != MRB_DUMP_OK)
            goto error_exit;
        bin += len;
    }
    result = nirep;
error_exit:
    if (result < MRB_DUMP_OK) {
        irep_free(sirep,mrb);
    }
    return result;
}

static int
read_rite_lineno_record(mrb_state *mrb, const uint8_t *bin, size_t irepno, uint32_t *len)
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

    lines = (uint16_t *)mrb->gc()._malloc(niseq * sizeof(uint16_t));
    if (lines == nullptr) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    for (i = 0; i < niseq; i++) {
        lines[i] = bin_to_uint16(bin);
        bin += sizeof(uint16_t); // niseq
        *len += sizeof(uint16_t);
    }

    mrb->m_irep[irepno]->filename = fname;
    mrb->m_irep[irepno]->lines = lines;

    return ret;
}

static int
read_rite_section_lineno(mrb_state *mrb, const uint8_t *bin, size_t sirep)
{
    int result;
    size_t i;
    uint32_t len;
    uint16_t nirep;
    uint16_t n;
    const struct rite_section_lineno_header *header;

    len = 0;
    header = (const struct rite_section_lineno_header*)bin;
    bin += sizeof(struct rite_section_lineno_header);

    nirep = bin_to_uint16(header->nirep);

    //Read Binary Data Section
    for (n = 0, i = sirep; n < nirep; n++, i++) {
        result = read_rite_lineno_record(mrb, bin, i, &len);
        if (result != MRB_DUMP_OK)
            goto error_exit;
        bin += len;
    }

    result = sirep + bin_to_uint16(header->sirep);
error_exit:
    return result;
}


static int
read_rite_binary_header(const uint8_t *bin, size_t *bin_size, uint16_t *crc)
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

int32_t
mrb_read_irep(mrb_state *mrb, const uint8_t *bin)
{
    int result;
    int32_t total_nirep = 0;
    const struct rite_section_header *section_header;
    uint16_t crc;
    size_t bin_size = 0;
    size_t n;
    size_t sirep;

    if ((mrb == NULL) || (bin == NULL)) {
        return MRB_DUMP_INVALID_ARGUMENT;
    }

    result = read_rite_binary_header(bin, &bin_size, &crc);
    if(result != MRB_DUMP_OK) {
        return result;
    }

    n = offset_crc_body();
    if(crc != calc_crc_16_ccitt(bin + n, bin_size - n, 0)) {
        return MRB_DUMP_INVALID_FILE_HEADER;
    }

    bin += sizeof(rite_binary_header);
    sirep = mrb->irep_len;

    do {
        section_header = (const rite_section_header *)bin;
        if(memcmp(section_header->section_identify, RITE_SECTION_IREP_IDENTIFIER, sizeof(section_header->section_identify)) == 0) {
            result = read_rite_section_irep(mrb, bin);
            if(result < MRB_DUMP_OK) {
                return result;
            }
            total_nirep += result;
        }
        else if(memcmp(section_header->section_identify, RITE_SECTION_LINENO_IDENTIFIER, sizeof(section_header->section_identify)) == 0) {
            result = read_rite_section_lineno(mrb, bin, sirep);
            if(result < MRB_DUMP_OK) {
                return result;
            }
        }
        bin += bin_to_uint32(section_header->section_size);
    } while(memcmp(section_header->section_identify, RITE_BINARY_EOF, sizeof(section_header->section_identify)) != 0);

    return sirep;
}

static void
irep_error(mrb_state *mrb, int n)
{
    static const char msg[] = "irep load error";
    mrb->m_exc = mrb_obj_ptr(mrb_exc_new(E_SCRIPT_ERROR, msg, sizeof(msg) - 1));
}

mrb_value
mrb_load_irep(mrb_state *mrb, const uint8_t *bin)
{
    int32_t n;

    n = mrb_read_irep(mrb, bin);
    if (n < 0) {
        irep_error(mrb, n);
        return mrb_nil_value();
    }
    return mrb->mrb_run(mrb_proc_new(mrb, mrb->m_irep[n]), mrb_top_self(mrb));
}

#ifdef ENABLE_STDIO
typedef int ( *record_reader_func)(mrb_state *, const uint8_t *, size_t , uint32_t *);
static int read_common(mrb_state *mrb,size_t sirep, size_t record_header_size,FILE *fp,record_reader_func func) {

    uint32_t len;
    rite_section_lineno_header header;
    if (fread(&header, sizeof(struct rite_section_lineno_header), 1, fp) == 0) {
        return MRB_DUMP_READ_FAULT;
    }

    int nirep = bin_to_uint16(header.nirep);
    int buf_size = record_header_size;
    uint8_t * buf = (uint8_t *)mrb->gc()._malloc(buf_size);
    //Read Binary Data Section
    int result = sirep+bin_to_uint16(header.sirep);
    int i = sirep;
    uint16_t n;
    for (n = 0; n < nirep; n++, i++) {
        void *ptr;
        if (fread(buf, record_header_size, 1, fp) == 0) {
            result = MRB_DUMP_READ_FAULT;
            break;
        }
        buf_size = bin_to_uint32(&buf[0]);
        ptr = mrb->gc()._realloc(buf, buf_size);
        if (!ptr) {
            result = MRB_DUMP_GENERAL_FAILURE;
            break;
        }
        buf = (uint8_t *)ptr;

        if (fread(&buf[record_header_size], buf_size - record_header_size, 1, fp) == 0) {
            result = MRB_DUMP_READ_FAULT;
            break;
        }
        int res = func(mrb, buf, i, &len);
        if (res != MRB_DUMP_OK) {
            result = res;
            break;
        }
    }
    if(n!=nirep) {
        assert(result < MRB_DUMP_OK);
        irep_free(sirep,mrb);
    }
    mrb->gc()._free(buf);
    return result;
}
static int32_t read_rite_section_lineno_file(mrb_state *mrb, FILE *fp, size_t sirep)
{
    const size_t record_header_size = 4;
    return read_common(mrb,sirep,record_header_size,fp,read_rite_lineno_record);
}

static int32_t
read_rite_section_irep_file(mrb_state *mrb, FILE *fp)
{
    size_t sirep = mrb->irep_len;
    const size_t record_header_size = 1 + 4;
    return read_common(mrb,sirep,record_header_size,fp,read_rite_irep_record);
}

int32_t
mrb_read_irep_file(mrb_state *mrb, FILE* fp)
{
    int result;
    int32_t total_nirep = 0;
    uint8_t *buf;
    uint16_t crc, crcwk = 0;
    uint32_t section_size = 0;
    size_t nbytes;
    size_t sirep;
    struct rite_section_header section_header;
    long fpos;
    size_t block_size = 1 << 14;
    const uint8_t block_fallback_count = 4;
    int i;
    const size_t buf_size = sizeof(struct rite_binary_header);

    if ((mrb == NULL) || (fp == NULL)) {
        return MRB_DUMP_INVALID_ARGUMENT;
    }

    buf = (uint8_t *)mrb->gc()._malloc(buf_size);
    if (fread(buf, buf_size, 1, fp) == 0) {
        mrb->gc()._free(buf);
        return MRB_DUMP_READ_FAULT;
    }
    result = read_rite_binary_header(buf, NULL, &crc);
    mrb->gc()._free(buf);
    if(result != MRB_DUMP_OK) {
        return result;
    }

    /* verify CRC */
    fpos = ftell(fp);
    /* You don't need use SIZE_ERROR as block_size is enough small. */
    buf = (uint8_t *)mrb->gc()._malloc(block_size);
    for (i = 0; i < block_fallback_count; i++,block_size >>= 1){
        buf =(uint8_t *)mrb->gc()._malloc( block_size);
        if (buf) break;
    }
    if (!buf) {
        return MRB_DUMP_GENERAL_FAILURE;
    }
    fseek(fp, offset_crc_body(), SEEK_SET);
    while((nbytes = fread(buf, 1, block_size, fp)) > 0) {
        crcwk = calc_crc_16_ccitt(buf, nbytes, crcwk);
    }
    mrb->gc()._free(buf);
    if (nbytes == 0 && ferror(fp)) {
        return MRB_DUMP_READ_FAULT;
    }
    if(crcwk != crc) {
        return MRB_DUMP_INVALID_FILE_HEADER;
    }
    fseek(fp, fpos + section_size, SEEK_SET);
    sirep = mrb->irep_len;

    // read sections
    do {
        fpos = ftell(fp);
        if (fread(&section_header, sizeof(struct rite_section_header), 1, fp) == 0) {
            return MRB_DUMP_READ_FAULT;
        }
        section_size = bin_to_uint32(section_header.section_size);

        if(memcmp(section_header.section_identify, RITE_SECTION_IREP_IDENTIFIER, sizeof(section_header.section_identify)) == 0) {
            fseek(fp, fpos, SEEK_SET);
            result = read_rite_section_irep_file(mrb, fp);
            if(result < MRB_DUMP_OK) {
                return result;
            }
            total_nirep += result;
        }
        else if(memcmp(section_header.section_identify, RITE_SECTION_LINENO_IDENTIFIER, sizeof(section_header.section_identify)) == 0) {
            fseek(fp, fpos, SEEK_SET);
            result = read_rite_section_lineno_file(mrb, fp, sirep);
            if(result < MRB_DUMP_OK) {
                return result;
            }
        }

        fseek(fp, fpos + section_size, SEEK_SET);
    } while(memcmp(section_header.section_identify, RITE_BINARY_EOF, sizeof(section_header.section_identify)) != 0);

    return total_nirep;
}

mrb_value
mrb_load_irep_file(mrb_state *mrb, FILE* fp)
{
    int n = mrb_read_irep_file(mrb, fp);

    if (n < 0) {
        irep_error(mrb, n);
        return mrb_nil_value();
    }
    return mrb->mrb_run(mrb_proc_new(mrb, mrb->m_irep[n]), mrb_top_self(mrb));
}
#endif /* ENABLE_STDIO */
