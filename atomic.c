#include "atomic.h"

/***********
 * Atomize *
 ***********/

#include "ksort.h"
#define atom_lt(a, b) (bcf_atom_cmp(&(a), &(b)) < 0)
KSORT_INIT(atom, bcf_atom_t, atom_lt)

static int bcf_atom_gen_at(const bcf_hdr_t *h, bcf1_t *b, int n, bcf_atom_t *a)
{
	int i, j, k, *eq, id_GT, *tr, has_dup = 0;
	bcf_fmt_t *gt;

	id_GT = bcf_id2int(h, BCF_DT_ID, "GT");
	assert(id_GT >= 0);
	bcf_unpack(b, BCF_UN_FMT);
	for (i = 0; i < b->n_fmt; ++i)
		if (b->d.fmt[i].id == id_GT) break;
	assert(i < b->n_fmt);
	gt = &b->d.fmt[i];
	assert(gt->n == 2);

	eq = (int*)alloca(n * sizeof(int));
	ks_introsort(atom, n, a);
	for (i = 1, eq[0] = 0; i < n; ++i) {
		eq[i] = bcf_atom_cmp(&a[i-1], &a[i])? i : eq[i-1];
		if (eq[i] == eq[i-1]) has_dup = 1;
	}

	tr = (int*)alloca(b->n_allele * sizeof(int));
	tr[0] = 0;
	for (k = 0; k < n; ++k) {
		int m;
		bcf_atom_t *ak = &a[k];
		if (eq[k] != k) continue; // duplicated atom
		for (i = 1; i < b->n_allele; ++i) tr[i] = 0;
		for (i = 0; i < n; ++i) { // WARNING: quadratic in the number of atoms
			if (eq[i] == eq[k]) // identical allele
				tr[a[i].anum] = 1;
			else if (a[i].pos < ak->pos + ak->rlen && ak->pos < a[i].pos + a[i].rlen) // overlap
				tr[a[i].anum] = 3;
		}
		ak->gt = (uint8_t*)realloc(ak->gt, b->n_sample * gt->n);
		for (i = 0, m = 0; i < b->n_sample; ++i, m += gt->n) {
			for (j = 0; j < gt->n; ++j) {
				int c = (int)(gt->p[m+j] >> 1) - 1;
				ak->gt[m+j] = c < 0? 2 : tr[c];
			}
		}
	}

	if (has_dup) {
		bcf_atom_t *swap;
		swap = (bcf_atom_t*)alloca(n * sizeof(bcf_atom_t));
		memcpy(swap, a, n * sizeof(bcf_atom_t));
		for (i = j = 0, k = n - 1; i < n; ++i) {
			if (eq[i] == i) a[j++] = swap[i];
			else a[k--] = swap[i];
		}
		n = j;
	}
	return n;
}

static void bcf_add_atom(bcf_atom_v *a, int rid, int pos, int rlen, int anum, int l_ref, const char *ref, int l_alt, const char *alt)
{
	bcf_atom_t *p;
	if (a->n == a->m) {
		int oldm = a->m;
		a->m = a->m? a->m<<1 : 4;
		a->a = (bcf_atom_t*)realloc(a->a, a->m * sizeof(bcf_atom_t));
		memset(a->a + oldm, 0, (a->m - oldm) * sizeof(bcf_atom_t));
	}
	p = &a->a[a->n++];
	p->rid = rid, p->pos = pos, p->rlen = rlen, p->anum = anum;
	p->ref.l = 0;
	if (l_ref < 0) l_ref = strlen(ref);
	if (l_alt < 0) l_alt = strlen(alt);
	kputsn(ref, l_ref, &p->ref); kputc('\0', &p->ref);
	kputsn(alt, l_alt, &p->ref);
	p->alt = p->ref.s + l_ref + 1;
}

void bcf_atomize(const bcf_hdr_t *h, bcf1_t *b, bcf_atom_v *a)
{
	int i, cid, l_ref, l_cigar = 0, old_n = a->n;
	kstring_t cigar = {0,0,0};
	char *p_cigar = 0, *p = 0;

	cid = bcf_id2int(h, BCF_DT_ID, "CIGAR");
	if (cid >= 0) {
		bcf_unpack(b, BCF_UN_INFO);
		for (i = 0; i < b->n_info; ++i)
			if (b->d.info[i].key == cid) break;
		assert(i < b->n_info); // may happen if CIGAR is in FILTER or FORMAT only
		assert(b->d.info[i].type == BCF_BT_CHAR);
		p_cigar = (char*)b->d.info[i].vptr;
		l_cigar = b->d.info[i].len;
		p = p_cigar;
	} else bcf_unpack(b, BCF_UN_STR);
	l_ref = strlen(b->d.allele[0]);

	for (i = 1; i < b->n_allele; ++i) { // traverse each allele
		char *p;
		int x, y, j, l, l_alt;
		l_alt = strlen(b->d.allele[i]);
		if (b->rlen != l_ref || (b->d.allele[i][0] == '<' && b->d.allele[i][l_alt-1] == '>')) { // symbolic allele
			bcf_add_atom(a, b->rid, b->pos, b->rlen, i, -1, b->d.allele[0], -1, b->d.allele[i]);
			continue;
		}
		// extract or generate CIGAR between REF and the current ALT
		cigar.l = 0;
		if (p_cigar) {
			for (p = p_cigar; p < p_cigar + l_cigar && *p != ','; ++p);
			assert(p - p_cigar > 0); // otherwise, incomplete CIGAR
			kputsn(p_cigar, p - p_cigar, &cigar);
			l_cigar -= p - p_cigar;
			p_cigar = p;
		} else if (strlen(b->d.allele[i]) == b->rlen) {
			kputw(b->rlen, &cigar);
			kputc('M', &cigar);
		} else {
			int rest;
			l = l_alt - b->rlen;
			kputsn("1M", 2, &cigar);
			if (l > 0) {
				kputw(l, &cigar);
				kputc('I', &cigar);
				rest = b->rlen - 1;
			} else {
				kputw(-l, &cigar);
				kputc('D', &cigar);
				rest = l_alt - 1;
			}
			if (rest) {
				kputw(rest, &cigar);
				kputc('M', &cigar);
			}
		}
		// extract difference
		x = y = 0;
		for (p = cigar.s; *p; ++p) {
			l = strtol(p, &p, 10);
			if (*p == 'M' || *p == '=' || *p == 'X') {
				for (j = 0; j < l; ++j)
					if (b->d.allele[0][x+j] != b->d.allele[i][y+j])
						bcf_add_atom(a, b->rid, b->pos + x + j, 1, i, 1, &b->d.allele[0][x+j], 1, &b->d.allele[i][y+j]);
				x += l, y += l;
			} else if (*p == 'I') {
				assert(x > 0 && y > 0);
				bcf_add_atom(a, b->rid, b->pos + x - 1, 1, i, 1, &b->d.allele[0][x-1], l+1, &b->d.allele[i][y-1]);
				y += l;
			} else if (*p == 'D') {
				assert(x > 0 && y > 0);
				bcf_add_atom(a, b->rid, b->pos + x - 1, l+1, i, l+1, &b->d.allele[0][x-1], 1, &b->d.allele[i][y-1]);
				x += l;
			}
		}
	}
	free(cigar.s);
	a->n = old_n + bcf_atom_gen_at(h, b, a->n - old_n, &a->a[old_n]);
}