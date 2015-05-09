#ifndef BCF_H
#define BCF_H

#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include "bgzf.h"
#include "hts.h"

/*****************
 * Header struct *
 *****************/

#define BCF_HL_FLT  0 // header line
#define BCF_HL_INFO 1
#define BCF_HL_FMT  2
#define BCF_HL_CTG  3

#define BCF_HT_FLAG 0 // header type
#define BCF_HT_INT  1
#define BCF_HT_REAL 2
#define BCF_HT_STR  3

#define BCF_VL_FIXED 0 // variable length
#define BCF_VL_VAR   1
#define BCF_VL_A     2
#define BCF_VL_G     3

/* === Dictionary ===

   The header keeps three dictonaries. The first keeps IDs in the
   "FILTER/INFO/FORMAT" lines, the second keeps the sequence names and lengths
   in the "contig" lines and the last keeps the sample names. bcf_hdr_t::dict[]
   is the actual hash table, which is opaque to the end users. In the hash
   table, the key is the ID or sample name as a C string and the value is a
   bcf_idinfo_t struct. bcf_hdr_t::id[] points to key-value pairs in the hash
   table in the order that they appear in the VCF header. bcf_hdr_t::n[] is the
   size of the hash table or, equivalently, the length of the id[] arrays.
*/

#define BCF_DT_ID		0 // dictionary type
#define BCF_DT_CTG		1
#define BCF_DT_SAMPLE	2

typedef struct {
	uint32_t info[3]; // for each number => Number:20, var:4, Type:4, ColType:4
	int id;
} bcf_idinfo_t;

typedef struct {
	const char *key;
	const bcf_idinfo_t *val;
} bcf_idpair_t;

typedef struct {
	int32_t l_text, m_text, n[3];
	bcf_idpair_t *id[3];
	void *dict[3]; // ID dictionary, contig dict and sample dict
	char *text;
	kstring_t mem;
} bcf_hdr_t;

extern uint8_t bcf_type_shift[];

/**************
 * VCF record *
 **************/

#define BCF_BT_NULL		0
#define BCF_BT_INT8		1
#define BCF_BT_INT16	2
#define BCF_BT_INT32	3
#define BCF_BT_FLOAT	5
#define BCF_BT_CHAR		7

typedef struct {
	int id, n, type, size; // bcf_hdr_t::id[BCF_DT_ID][$id].key is the key in string; $size is the per-sample size
	uint8_t *p; // point to the data array
} bcf_fmt_t;

typedef struct {
	int key, type, len; // bcf_hdr_t::id[BCF_DT_ID][$key].key is the key in string; $len: the length of the vector
	union {
		int32_t i; // integer value
		float f;   // float value
	} v1; // only set if $len==1; for easier access
	uint8_t *vptr; // point to data array, excluding sized bytes
} bcf_info_t;

typedef struct {
	int m_fmt, m_info, m_str, m_allele, m_flt; // allocated size (high-water mark); do not change
	int n_flt; // # FILTER fields
	char *id, **allele; // ID, REF and ALT; allele[0] is the REF; all null terminated
	int *flt; // filter keys in the dictionary
	bcf_info_t *info; // INFO
	bcf_fmt_t *fmt; // FORMAT and individual sample
} bcf_dec_t;

typedef struct {
	int32_t rid;  // CHROM
	int32_t pos;  // POS
	int32_t rlen; // length of REF
	float qual;   // QUAL
	uint32_t n_info:16, n_allele:16;
	uint32_t n_fmt:8, n_sample:24;
	kstring_t shared, indiv;
	bcf_dec_t d; // lazy evaluation: $d is not generated by bcf_read1(), but by explicitly calling bcf_unpack()
	int unpacked; // allow calling bcf_unpack() repeatedly without redoing the work
	uint8_t *unpack_ptr; // keep the place before which unpack is done
} bcf1_t;

#define bcf_int8_missing  INT8_MIN
#define bcf_int16_missing INT16_MIN
#define bcf_int32_missing INT32_MIN
#define bcf_int8_end  (INT8_MIN +1)
#define bcf_int16_end (INT16_MIN+1)
#define bcf_int32_end (INT32_MIN+1)

/*******
 * API *
 *******/

#ifdef __cplusplus
extern "C" {
#endif

	/***************
	 *** BCF I/O ***
	 ***************/

	bcf_hdr_t *bcf_hdr_init(void);
	int bcf_hdr_parse(bcf_hdr_t *h);

	/**
	 * Read BCF header
	 *
	 * @param fp     BGZF file pointer; file offset must be placed at the beginning
	 * 
	 * @return BCF header struct
	 */
	bcf_hdr_t *bcf_hdr_read(BGZF *fp);

	/**
	 * Write BCF header to BCF
	 *
	 * @param fp    BGZF file pointer; file offset placed at the beginning
	 * @param h     BCF header
	 */
	void bcf_hdr_write(BGZF *fp, const bcf_hdr_t *h);

	/** Destroy a BCF header struct */
	void bcf_hdr_destroy(bcf_hdr_t *h);

	/** Initialize a bcf1_t object; equivalent to calloc(1, sizeof(bcf1_t)) */
	bcf1_t *bcf_init1();
	
	/** Deallocate a bcf1_t object */
	void bcf_destroy1(bcf1_t *v);
	void bcf_clear1(bcf1_t *v);

	/**
	 * Read one BCF record
	 *
	 * @param fp     BGZF file pointer
	 * @param v      BCF record read from $fp
	 *
	 * @return  0 on success; -1 on normal file end; <-1 on error
	 */
	int bcf_read1(BGZF *fp, bcf1_t *v);

	/**
	 * Write one BCF record
	 *
	 * @param fp     BGZF file pointer
	 * @param v      BCF record to write
	 *
	 * @return
	 */
	int bcf_write1(BGZF *fp, const bcf1_t *v);

	/** Helper function for the bcf_iter_next() macro; ignore it */
	int bcf_readrec(BGZF *fp, void *null, bcf1_t *v, int *tid, int *beg, int *end);

	#define BCF_UN_STR  1 // up to ALT inclusive
	#define BCF_UN_FLT  2 // up to FILTER
	#define BCF_UN_INFO 4 // up to INFO
	#define BCF_UN_SHR  (BCF_UN_STR|BCF_UN_FLT|BCF_UN_INFO) // all shared information
	#define BCF_UN_FMT  8 // unpack format and each sample
	#define BCF_UN_IND  BCF_UN_FMT // a synonymous of BCF_UN_FMT
	#define BCF_UN_ALL  (BCF_UN_SHR|BCF_UN_FMT) // everything

	/**
	 * Unpack/decode a BCF record (fill the bcf1_t::d field
	 */
	int bcf_unpack(bcf1_t *b, int which); // to unpack everything, set $which to BCF_UN_ALL

	int bcf_id2int(const bcf_hdr_t *h, int which, const char *id);
	int bcf_name2id(const bcf_hdr_t *h, const char *id);

	void bcf_fmt_array(kstring_t *s, int n, int type, void *data);
	uint8_t *bcf_fmt_sized_array(kstring_t *s, uint8_t *ptr);

	void bcf_enc_vchar(kstring_t *s, int l, const char *a);
	void bcf_enc_vint(kstring_t *s, int n, const int32_t *a, int wsize);
	void bcf_enc_vfloat(kstring_t *s, int n, float *a);
	
	/*****************
	 *** BCF index ***
	 *****************/

	#define bcf_itr_destroy(iter) hts_itr_destroy(iter)
	#define bcf_itr_queryi(idx, tid, beg, end) hts_itr_query((idx), (tid), (beg), (end))
	#define bcf_itr_querys(idx, hdr, s) hts_itr_querys((idx), (s), (hts_name2id_f)(bcf_name2id), (hdr))
	#define bcf_itr_next(fp, itr, r) hts_itr_next((fp), (itr), (r), (hts_readrec_f)(bcf_readrec), 0)
	#define bcf_index_load(fn) hts_idx_load(fn, HTS_FMT_CSI)

	int bcf_index_build(const char *fn, int min_shift);

	/***************
	 *** VCF I/O ***
	 ***************/

	typedef htsFile vcfFile;
	#define vcf_open(fn, mode, fn_ref) hts_open((fn), (mode), (fn_ref)) // strchr(mode, 'b')!=0 for BCF; otherwise VCF
	#define vcf_close(fp) hts_close(fp)

	bcf_hdr_t *vcf_hdr_read(htsFile *fp);
	void vcf_hdr_write(htsFile *fp, const bcf_hdr_t *h);

	int vcf_parse1(kstring_t *s, const bcf_hdr_t *h, bcf1_t *v);
	int vcf_format1(const bcf_hdr_t *h, const bcf1_t *v, kstring_t *s);
	int vcf_read1(htsFile *fp, const bcf_hdr_t *h, bcf1_t *v);
	int vcf_write1(htsFile *fp, const bcf_hdr_t *h, const bcf1_t *v);

	/*************************
	 *** VCF/BCF utilities ***
	 *************************/

	int bcf_hdr_append(bcf_hdr_t *h, const char *line);
	bcf_hdr_t *bcf_hdr_subset(const bcf_hdr_t *h0, int n, char *const* samples, int *imap);
	int bcf_subset(const bcf_hdr_t *h, bcf1_t *v, int n, int *imap);
	int bcf_is_snp(bcf1_t *v);

	char *bcf_get_alt1(const bcf1_t *b, int *len);
	void bcfcpy(bcf1_t *dst, const bcf1_t *src);
	int bcfcmp(const bcf1_t *a, const bcf1_t *b);
	int bcfcpy_min(bcf1_t *b, const bcf1_t *b0, const char *alt2);
	int bcf_append_info_ints(const bcf_hdr_t *h, bcf1_t *b, const char *key, int n, const int32_t *val);

#ifdef __cplusplus
}
#endif

/*******************
 * Typed value I/O *
 *******************/

#include "kstring.h"

static inline void bcf_enc_size(kstring_t *s, int size, int type)
{
	if (size >= 15) {
		kputc(15<<4|type, s);
		if (size >= 128) {
			if (size >= 32768) {
				int32_t x = size;
				kputc(1<<4|BCF_BT_INT32, s);
				kputsn((char*)&x, 4, s);
			} else {
				int16_t x = size;
				kputc(1<<4|BCF_BT_INT16, s);
				kputsn((char*)&x, 2, s);
			}
		} else {
			kputc(1<<4|BCF_BT_INT8, s);
			kputc(size, s);
		}
	} else kputc(size<<4|type, s);
}

static inline int bcf_enc_inttype(long x)
{
	if (x <= INT8_MAX && x > INT8_MIN) return BCF_BT_INT8;
	if (x <= INT16_MAX && x > INT16_MIN) return BCF_BT_INT16;
	return BCF_BT_INT32;
}

static inline void bcf_enc_int1(kstring_t *s, int32_t x)
{
	if (x == INT32_MIN) {
		bcf_enc_size(s, 1, BCF_BT_INT8);
		kputc(INT8_MIN, s);
	} else if (x <= INT8_MAX && x > INT8_MIN) {
		bcf_enc_size(s, 1, BCF_BT_INT8);
		kputc(x, s);
	} else if (x <= INT16_MAX && x > INT16_MIN) {
		int16_t z = x;
		bcf_enc_size(s, 1, BCF_BT_INT16);
		kputsn((char*)&z, 2, s);
	} else {
		int32_t z = x;
		bcf_enc_size(s, 1, BCF_BT_INT32);
		kputsn((char*)&z, 4, s);
	}
}

static inline int32_t bcf_dec_int1(const uint8_t *p, int type, uint8_t **q)
{
	if (type == BCF_BT_INT8) {
		*q = (uint8_t*)p + 1;
		return *(int8_t*)p;
	} else if (type == BCF_BT_INT16) {
		*q = (uint8_t*)p + 2;
		return *(int16_t*)p;
	} else {
		*q = (uint8_t*)p + 4;
		return *(int32_t*)p;
	}
}

static inline int32_t bcf_dec_typed_int1(const uint8_t *p, uint8_t **q)
{
	return bcf_dec_int1(p + 1, *p&0xf, q);
}

static inline int32_t bcf_dec_size(const uint8_t *p, uint8_t **q, int *type)
{
	*type = *p & 0xf;
	if (*p>>4 != 15) {
		*q = (uint8_t*)p + 1;
		return *p>>4;
	} else return bcf_dec_typed_int1(p + 1, q);
}

#endif
