#define _XOPEN_SOURCE 700
#include "common/selector.h"
#include "generator.h"
#include "amtail_pcre.h"
#include "vm.h"
#include "variables.h"
#include "dstructures/ht.h"
#include "dstructures/tommy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

static void amtail_invoke_var_touched(amtail_thread *t, amtail_variable *v)
{
	if (!t || !t->touch.on_var_touched || !v)
		return;
	if (v->is_template || v->hidden)
		return;
	if (!v->export_name || !v->export_name->s)
		return;
	t->touch.on_var_touched(t->touch.userdata, v);
}

void (*amtail_vmfunc[256])(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll);
void amtail_vmstack_push(amtail_thread *amt_thread, amtail_byteop *byte_ops);
static char* amtail_vm_resolve_string(amtail_byteop *op, alligator_ht *variables, amtail_thread *t);
static void amtail_vm_captures_reset(amtail_thread *t);
static int amtail_vm_resolve_capture_token(const char *key, size_t token_len, amtail_thread *t,
	alligator_ht *variables, const char **emit, size_t *emit_len);
static char *amtail_vm_lookup_variable_string(const char *name, alligator_ht *variables, amtail_thread *t);
static uint8_t amtail_vm_do_split(amtail_thread *t, const char *sep, size_t sep_len, const char *str, size_t str_len);
static void amtail_vm_split_publish_captures(amtail_thread *t);
static void amtail_vm_split_bind_current(amtail_thread *t);

static void amtail_vm_split_arrays_reset(amtail_thread *t)
{
	if (!t)
		return;
	for (uint8_t i = 0; i < t->split_array_count; ++i)
	{
		free(t->split_arrays[i].storage);
		t->split_arrays[i].storage = NULL;
		t->split_arrays[i].count = 0;
	}
	t->split_array_count = 0;
}

static void amtail_vm_captures_reset(amtail_thread *t)
{
	if (!t)
		return;
	t->capture_count = 0;
	t->named_capture_count = 0;
	free(t->split_storage);
	t->split_storage = NULL;
	t->split_count = 0;
	t->split_index = 0;
	t->split_bind_len = 0;
	t->split_active = 0;
	t->split_loop_array = NULL;
	amtail_vm_split_arrays_reset(t);
}

static int amtail_vm_parse_index_token(const char *idx, size_t idx_len, alligator_ht *variables,
	amtail_thread *t, unsigned *out_idx)
{
	if (!idx || !idx_len || !out_idx)
		return 0;

	const char *p = idx;
	size_t l = idx_len;
	if (p[0] == '$' && l > 1)
	{
		++p;
		--l;
	}
	if (!l)
		return 0;

	unsigned idx_val = 0;
	int all_digits = 1;
	for (size_t i = 0; i < l; ++i)
	{
		if (p[i] < '0' || p[i] > '9')
		{
			all_digits = 0;
			break;
		}
		idx_val = idx_val * 10u + (unsigned)(p[i] - '0');
	}
	if (all_digits)
	{
		*out_idx = idx_val;
		return 1;
	}

	if (!variables)
		return 0;

	char namebuf[AMTAIL_SPLIT_BIND_MAX];
	if (l >= sizeof(namebuf))
		l = sizeof(namebuf) - 1;
	memcpy(namebuf, p, l);
	namebuf[l] = '\0';

	amtail_lookup_key lk = { namebuf, l };
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk,
		amtail_hash(namebuf, l));
	if (!var || var->type != ALLIGATOR_VARTYPE_COUNTER)
		return 0;

	if (var->i < 0)
		return 0;
	*out_idx = (unsigned)var->i;
	return 1;
}

static amtail_named_split_array *amtail_vm_split_array_find(amtail_thread *t, const char *name, size_t name_len)
{
	if (!t || !name || !name_len)
		return NULL;

	if (name[0] == '$' && name_len > 1)
	{
		++name;
		--name_len;
	}

	for (uint8_t i = 0; i < t->split_array_count; ++i)
	{
		if (t->split_arrays[i].name_len == name_len &&
		    memcmp(t->split_arrays[i].name, name, name_len) == 0)
			return &t->split_arrays[i];
	}
	return NULL;
}

static amtail_named_split_array *amtail_vm_split_array_get_or_create(amtail_thread *t, const char *name, size_t name_len)
{
	amtail_named_split_array *arr = amtail_vm_split_array_find(t, name, name_len);
	if (arr)
		return arr;
	if (!t || t->split_array_count >= AMTAIL_SPLIT_ARRAYS_MAX)
		return NULL;

	arr = &t->split_arrays[t->split_array_count++];
	if (name_len >= AMTAIL_SPLIT_BIND_MAX)
		name_len = AMTAIL_SPLIT_BIND_MAX - 1;
	memcpy(arr->name, name, name_len);
	arr->name_len = (uint8_t)name_len;
	arr->storage = NULL;
	arr->count = 0;
	return arr;
}

static void amtail_vm_split_array_commit_from_global(amtail_thread *t, const char *name, size_t name_len)
{
	if (!t || !t->split_count || !name || !name_len)
		return;

	amtail_named_split_array *arr = amtail_vm_split_array_get_or_create(t, name, name_len);
	if (!arr)
		return;

	free(arr->storage);
	arr->storage = t->split_storage;
	t->split_storage = NULL;
	arr->count = t->split_count;
	for (uint8_t i = 0; i < arr->count; ++i)
		arr->parts[i] = t->split_parts[i];
	t->split_count = 0;
}

static uint8_t amtail_vm_do_split(amtail_thread *t, const char *sep, size_t sep_len, const char *str, size_t str_len)
{
	if (!t || !sep || !sep_len || !str)
		return 0;

	free(t->split_storage);
	t->split_storage = NULL;
	t->split_count = 0;
	t->split_index = 0;
	t->split_active = 0;

	if (!str_len)
		return 0;

	t->split_storage = malloc(str_len + 1);
	if (!t->split_storage)
		return 0;
	memcpy(t->split_storage, str, str_len);
	t->split_storage[str_len] = '\0';

	const char *cur = t->split_storage;
	const char *end = t->split_storage + str_len;

	while (cur <= end && t->split_count < AMTAIL_SPLIT_MAX)
	{
		const char *next = NULL;
		if (sep_len == 1)
			next = memchr(cur, sep[0], (size_t)(end - cur));
		else
		{
			for (const char *p = cur; p + sep_len <= end; ++p)
			{
				if (memcmp(p, sep, sep_len) == 0)
				{
					next = p;
					break;
				}
			}
		}

		if (!next)
		{
			size_t chunk = (size_t)(end - cur);
			if (chunk > 0 || t->split_count == 0)
			{
				t->split_parts[t->split_count].ptr = cur;
				t->split_parts[t->split_count].len = (uint32_t)chunk;
				++t->split_count;
			}
			break;
		}

		t->split_parts[t->split_count].ptr = cur;
		t->split_parts[t->split_count].len = (uint32_t)(next - cur);
		++t->split_count;
		cur = next + sep_len;
	}

	return t->split_count;
}

static void amtail_vm_split_publish_captures(amtail_thread *t)
{
	if (!t)
		return;

	uint8_t max_group = t->split_count;
	if (max_group >= AMTAIL_CAPTURE_MAX)
		max_group = AMTAIL_CAPTURE_MAX - 1;
	t->capture_count = max_group;

	for (uint8_t i = 0; i < max_group; ++i)
	{
		t->captures[i + 1].ptr = t->split_parts[i].ptr;
		t->captures[i + 1].len = t->split_parts[i].len;
	}
}

static void amtail_vm_split_bind_current(amtail_thread *t)
{
	if (!t || !t->split_bind_len || !t->split_loop_array)
		return;

	if (t->split_index >= t->split_loop_array->count)
		return;

	amtail_capture_slice slice = t->split_loop_array->parts[t->split_index];
	uint8_t found = 0;
	for (uint8_t i = 0; i < t->named_capture_count; ++i)
	{
		if (t->named_captures[i].name_len == t->split_bind_len &&
		    memcmp(t->named_captures[i].name, t->split_bind, t->split_bind_len) == 0)
		{
			t->named_captures[i].slice = slice;
			found = 1;
			break;
		}
	}
	if (!found && t->named_capture_count < AMTAIL_CAPTURE_MAX)
	{
		amtail_named_capture_slot *slot = &t->named_captures[t->named_capture_count++];
		slot->name = t->split_bind;
		slot->name_len = t->split_bind_len;
		slot->slice = slice;
	}
}

static void amtail_vm_apply_numbered_captures(amtail_thread *t, char *line, uint64_t line_size, const int *ovector, int count)
{
	if (!t || !line || !ovector || count <= 1)
		return;

	int max_group = count - 1;
	if (max_group >= AMTAIL_CAPTURE_MAX)
		max_group = AMTAIL_CAPTURE_MAX - 1;
	t->capture_count = (uint8_t)max_group;

	for (int i = 1; i <= max_group; ++i)
	{
		int start = ovector[2 * i];
		int end = ovector[2 * i + 1];
		if (start < 0 || end < start || (uint64_t)end > line_size)
		{
			t->captures[i].ptr = NULL;
			t->captures[i].len = 0;
			continue;
		}
		t->captures[i].ptr = line + start;
		t->captures[i].len = (uint32_t)(end - start);
	}
}

static int amtail_vm_resolve_capture_token(const char *key, size_t token_len, amtail_thread *t,
	alligator_ht *variables, const char **emit, size_t *emit_len)
{
	if (!key || !token_len || !t || !emit || !emit_len)
		return 0;

	const char *p = key;
	size_t l = token_len;
	if (p[0] == '$' && l > 1)
	{
		++p;
		--l;
	}
	if (!l)
		return 0;

	const char *bracket = memchr(p, '[', l);
	if (bracket && bracket > p)
	{
		size_t name_len = (size_t)(bracket - p);
		const char *idx_p = bracket + 1;
		size_t idx_len = (size_t)(p + l - idx_p);
		if (idx_len > 0 && idx_p[idx_len - 1] == ']')
			--idx_len;

		unsigned idx = 0;
		if (amtail_vm_parse_index_token(idx_p, idx_len, variables, t, &idx))
		{
			amtail_named_split_array *arr = amtail_vm_split_array_find(t, p, name_len);
			if (arr && idx < arr->count && arr->parts[idx].ptr && arr->parts[idx].len)
			{
				*emit = arr->parts[idx].ptr;
				*emit_len = arr->parts[idx].len;
				return 1;
			}
		}
	}

	if (t->split_active && t->split_bind_len && l == t->split_bind_len &&
	    memcmp(p, t->split_bind, l) == 0 && t->split_loop_array &&
	    t->split_index < t->split_loop_array->count)
	{
		const amtail_capture_slice *slice = &t->split_loop_array->parts[t->split_index];
		if (slice->ptr && slice->len)
		{
			*emit = slice->ptr;
			*emit_len = slice->len;
			return 1;
		}
	}

	unsigned idx = 0;
	int all_digits = 1;
	for (size_t i = 0; i < l; ++i)
	{
		if (p[i] < '0' || p[i] > '9')
		{
			all_digits = 0;
			break;
		}
		idx = idx * 10u + (unsigned)(p[i] - '0');
	}
	if (all_digits && idx > 0 && idx < AMTAIL_CAPTURE_MAX && idx <= t->capture_count)
	{
		if (t->captures[idx].ptr && t->captures[idx].len)
		{
			*emit = t->captures[idx].ptr;
			*emit_len = t->captures[idx].len;
			return 1;
		}
		return 0;
	}

	for (uint8_t i = 0; i < t->named_capture_count; ++i)
	{
		if (t->named_captures[i].name_len == l &&
		    memcmp(t->named_captures[i].name, p, l) == 0 &&
		    t->named_captures[i].slice.ptr && t->named_captures[i].slice.len)
		{
			*emit = t->named_captures[i].slice.ptr;
			*emit_len = t->named_captures[i].slice.len;
			return 1;
		}
	}
	return 0;
}

static void amtail_vm_apply_named_captures(amtail_thread *t, regex_match *rematch, char *line, uint64_t line_size, const int *ovector, int count)
{
	if (!t || !rematch || !line || !line_size || !rematch->regex_compiled || !ovector || count <= 0)
		return;

	t->named_capture_count = 0;

	int namecount = rematch->pcre_name_count;
	int entry_size = rematch->pcre_name_entry_size;
	const unsigned char *name_table = rematch->pcre_name_table;
	if (namecount <= 0 || entry_size <= 0 || !name_table)
		return;

	for (int i = 0; i < namecount; ++i)
	{
		const unsigned char *entry = name_table + (size_t)i * (size_t)entry_size;
		int group_index = ((int)entry[0] << 8) | (int)entry[1];
		if (group_index <= 0 || group_index >= count)
			continue;

		int start = ovector[2 * group_index];
		int end = ovector[2 * group_index + 1];
		if (start < 0 || end < start || (uint64_t)end > line_size)
			continue;

		if (entry_size <= 2)
			continue;
		const char *group_name = (const char *)(entry + 2);
		size_t name_max = (size_t)entry_size - 2u;
		size_t gnlen = strnlen(group_name, name_max);
		if (!gnlen)
			continue;

		if (t->named_capture_count >= AMTAIL_CAPTURE_MAX)
			break;
		amtail_named_capture_slot *slot = &t->named_captures[t->named_capture_count++];
		slot->name = group_name;
		slot->name_len = (uint8_t)gnlen;
		slot->slice.ptr = line + start;
		slot->slice.len = (uint32_t)(end - start);
	}
}

static void amtail_vm_apply_regex_captures(amtail_thread *t, regex_match *rematch, char *line, uint64_t line_size, const int *ovector, int count)
{
	amtail_vm_apply_numbered_captures(t, line, line_size, ovector, count);
	if (!rematch || !line || !rematch->regex_compiled || !ovector || count <= 0)
		return;
	if (rematch->pcre_name_count > 0)
		amtail_vm_apply_named_captures(t, rematch, line, line_size, ovector, count);
}

static int amtail_histogram_init(amtail_variable *var)
{
	if (!var || var->type != ALLIGATOR_VARTYPE_HISTOGRAM || !var->bucket_count || !var->bucket)
		return 0;

	if (!var->bucket_bounds)
	{
		var->bucket_bounds = calloc(var->bucket_count, sizeof(*var->bucket_bounds));
		if (!var->bucket_bounds)
			return 0;

		for (uint8_t i = 0; i < var->bucket_count; ++i)
		{
			if (!var->bucket[i] || !var->bucket[i]->s)
				continue;

			char *end = NULL;
			double bound = strtod(var->bucket[i]->s, &end);
			if (!end || *end != '\0')
				continue;
			var->bucket_bounds[i] = bound;
		}
	}

	if (!var->bucket_hits)
	{
		var->bucket_hits = calloc(var->bucket_count + 1, sizeof(*var->bucket_hits));
		if (!var->bucket_hits)
			return 0;
	}

	return 1;
}

static void amtail_histogram_observe(amtail_variable *var, double value)
{
	if (!var || !amtail_histogram_init(var))
		return;

	++var->histogram_count;
	var->histogram_sum += value;

	uint8_t i = 0;
	while (i < var->bucket_count && value > var->bucket_bounds[i])
		++i;
	for (; i <= var->bucket_count; ++i)
		++var->bucket_hits[i];
}

static void amtail_vm_free_tempop(amtail_byteop *op)
{
	if (!op || !op->allocated)
		return;

	if (op->vartype == ALLIGATOR_VARTYPE_TEXT && op->ls)
		string_free(op->ls);
	if (op->allocated == 1)
		free(op);
}

static void amtail_vm_stack_clear(amtail_thread *amt_thread)
{
	if (!amt_thread)
		return;

	while (amt_thread->stack_ptr)
	{
		amtail_byteop *op = amt_thread->stack[--amt_thread->stack_ptr];
		amtail_vm_free_tempop(op);
	}
	amt_thread->temp_pool_ptr = 0;
}

static amtail_byteop* amtail_vm_make_temp_value(amtail_thread *amt_thread)
{
	if (!amt_thread)
		return NULL;

	amtail_byteop *new = NULL;
	if (amt_thread->temp_pool_ptr < AMTAIL_VM_STACK_SIZE)
	{
		new = &amt_thread->temp_pool[amt_thread->temp_pool_ptr++];
		/* Avoid whole-struct memset in hot path; reset only runtime fields. */
		new->opcode = 0;
		new->export_name = NULL;
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
		new->facttype = ALLIGATOR_FACTTYPE_INT;
		new->hidden = 0;
		new->ld = 0;
		new->rd = 0;
		new->by_count = 0;
		new->by = NULL;
		new->bucket_count = 0;
		new->bucket = NULL;
		new->re_match = NULL;
		new->right_opcounter = 0;
		new->allocated = 2; /* pooled temp value */
	}
	else
	{
		/* Fallback for very large expressions. */
		new = calloc(1, sizeof(*new));
		if (!new)
			return NULL;
		new->allocated = 1; /* heap temp value */
	}

	amtail_vmstack_push(amt_thread, new);
	return new;
}

static int amtail_vm_get_number(amtail_byteop *op, double *out)
{
	if (!op || !out)
		return 0;

	if (op->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		*out = op->ld;
		return 1;
	}
	if (op->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		*out = (double)op->li;
		return 1;
	}

	return 0;
}

static void amtail_vm_push_bool(amtail_thread *amt_thread, int value)
{
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = value ? 1 : 0;
}

static int amtail_vm_compare_equal(amtail_byteop *left, amtail_byteop *right, alligator_ht *variables, amtail_thread *t)
{
	double l = 0, r = 0;
	if (amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r))
		return l == r;

	char *ls = amtail_vm_resolve_string(left, variables, t);
	char *rs = amtail_vm_resolve_string(right, variables, t);
	if (!ls || !rs)
	{
		free(ls);
		free(rs);
		return 0;
	}

	int eq = strcmp(ls, rs) == 0;
	free(ls);
	free(rs);
	return eq;
}

static int amtail_vm_extract_binary_operands(const char *expr, char **lhs, char **rhs)
{
	if (!expr || !lhs || !rhs)
		return 0;

	*lhs = NULL;
	*rhs = NULL;

	const char *p = expr;
	while (*p == ' ' || *p == '\t')
		++p;
	if (!*p)
		return 0;

	int in_quote = 0;
	char quote = '\0';
	const char *split = NULL;
	for (const char *it = p; *it; ++it)
	{
		if ((*it == '"' || *it == '\'') && (!in_quote || *it == quote))
		{
			if (!in_quote)
			{
				in_quote = 1;
				quote = *it;
			}
			else
			{
				in_quote = 0;
				quote = '\0';
			}
		}
		else if (!in_quote && (*it == ' ' || *it == '\t'))
		{
			split = it;
			break;
		}
	}

	if (!split)
		return 0;

	const char *rhs_start = split;
	while (*rhs_start == ' ' || *rhs_start == '\t')
		++rhs_start;

	size_t lhs_len = (size_t)(split - p);
	while (lhs_len && (p[lhs_len - 1] == ' ' || p[lhs_len - 1] == '\t'))
		--lhs_len;

	if (!lhs_len || !*rhs_start)
		return 0;

	*lhs = strndup(p, lhs_len);
	*rhs = strdup(rhs_start);
	if (!*lhs || !*rhs)
	{
		free(*lhs);
		free(*rhs);
		*lhs = NULL;
		*rhs = NULL;
		return 0;
	}

	return 1;
}

static int amtail_vm_token_to_number(const char *token, alligator_ht *variables, amtail_thread *t, double *out)
{
	if (!token || !out)
		return 0;

	char *end = NULL;
	double num = strtod(token, &end);
	if (end && *end == '\0')
	{
		*out = num;
		return 1;
	}

	char *resolved = amtail_vm_lookup_variable_string(token, variables, t);
	if (!resolved)
		return 0;
	end = NULL;
	num = strtod(resolved, &end);
	free(resolved);
	if (!end || *end != '\0')
		return 0;

	*out = num;
	return 1;
}

static char* amtail_vm_token_to_string(const char *token, alligator_ht *variables, amtail_thread *t)
{
	if (!token)
		return NULL;

	size_t len = strlen(token);
	if (len >= 2 &&
	    ((token[0] == '"' && token[len - 1] == '"') ||
	     (token[0] == '\'' && token[len - 1] == '\'')))
		return strndup(token + 1, len - 2);

	char *resolved = amtail_vm_lookup_variable_string(token, variables, t);
	if (resolved)
		return resolved;

	return strdup(token);
}

static int amtail_vm_cast_to_int64(amtail_byteop *val, alligator_ht *variables, amtail_thread *t, int64_t *out)
{
	if (!val || !out)
		return 0;

	double num = 0;
	if (amtail_vm_get_number(val, &num))
	{
		*out = (int64_t)num;
		return 1;
	}

	char *s = amtail_vm_resolve_string(val, variables, t);
	if (!s)
		return 0;

	char *end = NULL;
	long long parsed = strtoll(s, &end, 10);
	if (!end || *end != '\0')
	{
		free(s);
		return 0;
	}
	free(s);

	*out = (int64_t)parsed;
	return 1;
}

static int amtail_vm_cast_to_double(amtail_byteop *val, alligator_ht *variables, amtail_thread *t, double *out)
{
	if (!val || !out)
		return 0;

	double num = 0;
	if (amtail_vm_get_number(val, &num))
	{
		*out = num;
		return 1;
	}

	char *s = amtail_vm_resolve_string(val, variables, t);
	if (!s)
		return 0;

	char *end = NULL;
	double parsed = strtod(s, &end);
	int parse_ok = (end && *end == '\0');
	free(s);
	if (!parse_ok)
		return 0;

	*out = parsed;
	return 1;
}

static char* amtail_vm_strdup_trim_quotes(const char *src)
{
	if (!src)
		return NULL;

	size_t len = strlen(src);
	if (len >= 2 &&
	    ((src[0] == '"' && src[len - 1] == '"') ||
	     (src[0] == '\'' && src[len - 1] == '\'')))
		return strndup(src + 1, len - 2);

	return strdup(src);
}

static char* amtail_vm_resolve_string(amtail_byteop *op, alligator_ht *variables, amtail_thread *t)
{
	if (!op)
		return NULL;

	if (op->vartype == ALLIGATOR_VARTYPE_TEXT && op->ls && op->ls->s)
		return strdup(op->ls->s);
	if (op->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%"PRId64, op->li);
		return strdup(tmp);
	}
	if (op->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		char tmp[64];
		snprintf(tmp, sizeof(tmp), "%.17g", op->ld);
		return strdup(tmp);
	}

	if (op->export_name && op->export_name->s)
	{
		const char *emit = NULL;
		size_t emit_len = 0;
		if (t && amtail_vm_resolve_capture_token(op->export_name->s, op->export_name->l, t, variables, &emit, &emit_len) && emit_len)
		{
			char *copy = malloc(emit_len + 1);
			if (!copy)
				return NULL;
			memcpy(copy, emit, emit_len);
			copy[emit_len] = '\0';
			return copy;
		}

		if (variables)
		{
			amtail_lookup_key lk = { op->export_name->s, op->export_name->l };
			amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare,
			                                           &lk,
			                                           amtail_hash(op->export_name->s, op->export_name->l));
			if (var)
			{
				if (var->type == ALLIGATOR_VARTYPE_TEXT && var->s && var->s->s)
					return strdup(var->s->s);
				if (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_TEXT && var->s && var->s->s)
					return strdup(var->s->s);
				if (var->type == ALLIGATOR_VARTYPE_COUNTER)
				{
					char tmp[64];
					snprintf(tmp, sizeof(tmp), "%"PRId64, var->i);
					return strdup(tmp);
				}
				if (var->type == ALLIGATOR_VARTYPE_GAUGE)
				{
					char tmp[64];
					snprintf(tmp, sizeof(tmp), "%.17g", var->d);
					return strdup(tmp);
				}
			}
		}

		return amtail_vm_strdup_trim_quotes(op->export_name->s);
	}

	return NULL;
}

/* Writable key[token_len]==0; may memmove key for '$' strip. Borrowed *emit from var->s when no heap. */
static int amtail_vm_resolve_metric_token(char *key, size_t token_len, amtail_thread *t, alligator_ht *variables,
	const char **emit, size_t *emit_len, int *emit_heap_allocated)
{
	if (!variables || !token_len)
		return 0;

	*emit_heap_allocated = 0;
	if (t && amtail_vm_resolve_capture_token(key, token_len, t, variables, emit, emit_len))
		return 1;

	amtail_lookup_key lk = { key, token_len };
	uint32_t h = amtail_hash((char*)key, token_len);
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, h);
	if (!var && key[0] == '$' && token_len > 1)
	{
		size_t tl = token_len - 1;
		memmove(key, key + 1, tl + 1);
		h = amtail_hash((char*)key, tl);
		lk.p = key;
		lk.l = tl;
		var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, h);
	}

	if (!var)
		return 0;

	if (var->type == ALLIGATOR_VARTYPE_TEXT && var->s && var->s->s)
	{
		*emit = var->s->s;
		*emit_len = var->s->l;
		return 1;
	}
	if (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_TEXT && var->s && var->s->s)
	{
		*emit = var->s->s;
		*emit_len = var->s->l;
		return 1;
	}
	if (var->type == ALLIGATOR_VARTYPE_COUNTER || (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_INT))
	{
		char *buf = malloc(64);
		if (!buf)
			return 0;
		snprintf(buf, 64, "%"PRId64, var->i);
		*emit = buf;
		*emit_len = strlen(buf);
		*emit_heap_allocated = 1;
		return 1;
	}
	if (var->type == ALLIGATOR_VARTYPE_GAUGE || (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_DOUBLE))
	{
		char *buf = malloc(64);
		if (!buf)
			return 0;
		snprintf(buf, 64, "%.17g", var->d);
		*emit = buf;
		*emit_len = strlen(buf);
		*emit_heap_allocated = 1;
		return 1;
	}
	return 0;
}

static char *amtail_vm_lookup_variable_string(const char *name, alligator_ht *variables, amtail_thread *t)
{
	if (!name || !*name)
		return NULL;

	const char *emit = NULL;
	size_t emit_len = 0;
	if (t && amtail_vm_resolve_capture_token(name, strlen(name), t, variables, &emit, &emit_len) && emit_len)
	{
		char *copy = malloc(emit_len + 1);
		if (!copy)
			return NULL;
		memcpy(copy, emit, emit_len);
		copy[emit_len] = '\0';
		return copy;
	}

	if (!variables)
		return NULL;

	size_t name_len = strlen(name);
	amtail_lookup_key lk = { name, name_len };
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, amtail_hash((char*)name, name_len));
	if (!var && name[0] == '$' && name[1])
	{
		const char *trimmed = name + 1;
		size_t trimmed_len = strlen(trimmed);
		amtail_lookup_key lk2 = { trimmed, trimmed_len };
		var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk2, amtail_hash((char*)trimmed, trimmed_len));
	}

	if (!var)
		return NULL;

	if (var->type == ALLIGATOR_VARTYPE_TEXT && var->s && var->s->s)
		return strdup(var->s->s);
	if (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_TEXT && var->s && var->s->s)
		return strdup(var->s->s);
	if (var->type == ALLIGATOR_VARTYPE_COUNTER || (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_INT))
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%"PRId64, var->i);
		return strdup(buf);
	}
	if (var->type == ALLIGATOR_VARTYPE_GAUGE || (var->type == ALLIGATOR_VARTYPE_CONST && var->facttype == ALLIGATOR_FACTTYPE_DOUBLE))
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%.17g", var->d);
		return strdup(buf);
	}

	return NULL;
}

/* raw_len: logical length (e.g. string->l); 0 means use strlen(raw_key). */
static char *amtail_vm_interpolate_metric_key(const char *raw_key, size_t raw_len, amtail_thread *t, alligator_ht *variables)
{
	if (!raw_key)
		return NULL;

	if (raw_len == 0)
		raw_len = strlen(raw_key);

	int has_template = 0;
	for (size_t i = 0; i + 2 <= raw_len; ++i) {
		if (raw_key[i] == '[' && raw_key[i + 1] == '$') {
			has_template = 1;
			break;
		}
	}
	if (!has_template)
		return NULL;

	size_t cap = raw_len * 2u + 64u;
	if (cap < raw_len + 1u)
		cap = raw_len + 1u;
	char *out = malloc(cap);
	if (!out)
		return NULL;

	enum { TOK_STACK = 512 };
	char keybuf[TOK_STACK];

	size_t oi = 0;
	const char *raw_end = raw_key + raw_len;
	const char *p = raw_key;
	while (p < raw_end) {
		if (p + 1 < raw_end && p[0] == '[' && p[1] == '$') {
			const char *end = memchr(p + 2, ']', (size_t)(raw_end - (p + 2)));
			if (end) {
				const char *literal = p + 1;
				size_t token_len = (size_t)(end - literal);
				char *keyheap = NULL;
				char *key = keybuf;
				if (token_len + 1u > TOK_STACK) {
					keyheap = malloc(token_len + 1u);
					if (!keyheap) {
						free(out);
						return NULL;
					}
					key = keyheap;
				}
				memcpy(key, literal, token_len);
				key[token_len] = '\0';

				const char *emit;
				size_t emit_len;
				int emit_heap = 0;
				if (!amtail_vm_resolve_metric_token(key, token_len, t, variables, &emit, &emit_len, &emit_heap)) {
					emit = literal;
					emit_len = token_len;
				}

				if (oi + emit_len + 2u >= cap) {
					size_t ncap = (oi + emit_len + 2u) * 2u;
					if (ncap < cap * 2u)
						ncap = cap * 2u;
					char *nout = realloc(out, ncap);
					if (!nout) {
						if (emit_heap)
							free((void *)emit);
						free(keyheap);
						free(out);
						return NULL;
					}
					out = nout;
					cap = ncap;
				}

				out[oi++] = '[';
				if (emit_len) {
					memcpy(out + oi, emit, emit_len);
					oi += emit_len;
				}
				out[oi++] = ']';

				if (emit_heap)
					free((void *)emit);
				free(keyheap);
				p = end + 1;
				continue;
			}
		}

		if (oi + 2u >= cap) {
			size_t ncap = cap * 2u;
			char *nout = realloc(out, ncap);
			if (!nout) {
				free(out);
				return NULL;
			}
			out = nout;
			cap = ncap;
		}
		out[oi++] = *p++;
	}
	out[oi] = '\0';
	return out;
}

static void amtail_vm_push_text(amtail_thread *amt_thread, const char *text)
{
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	new->vartype = ALLIGATOR_VARTYPE_TEXT;
	new->ls = string_init_dup((char*)(text ? text : ""));
}

static double amtail_vm_parse_epoch_string(const char *s)
{
	if (!s)
		return 0;

	char *end = NULL;
	double v = strtod(s, &end);
	if (end && *end == '\0')
		return v;

	/* Minimal fallback for "YYYY-MM-DD HH:MM:SS" / "YYYY-MM-DDTHH:MM:SS". */
	int y = 0, mon = 0, d = 0, hh = 0, mm = 0, ss = 0;
	if (sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mon, &d, &hh, &mm, &ss) == 6 ||
	    sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mon, &d, &hh, &mm, &ss) == 6)
	{
		struct tm tmv;
		memset(&tmv, 0, sizeof(tmv));
		tmv.tm_year = y - 1900;
		tmv.tm_mon = mon - 1;
		tmv.tm_mday = d;
		tmv.tm_hour = hh;
		tmv.tm_min = mm;
		tmv.tm_sec = ss;
		time_t t = mktime(&tmv);
		if (t >= 0)
			return (double)t;
	}

	return 0;
}


amtail_byteop* amtail_vmstack_pop(amtail_thread *amt_thread)
{
	if (amt_thread->stack_ptr < 1)
		return NULL;

	return amt_thread->stack[--amt_thread->stack_ptr];
}

void amtail_vmstack_push(amtail_thread *amt_thread, amtail_byteop *byte_ops)
{
	if (amt_thread->stack_ptr + 1 >= AMTAIL_VM_STACK_SIZE)
	{
		fprintf(stderr, "amtail fatal: stack is oversized\n");
		return;
	}

	amt_thread->stack[amt_thread->stack_ptr++] = byte_ops;
}

void amtail_vmfunc_assign(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_vmstack_push(amt_thread, byte_ops);
}

void amtail_vmfunc_var_use(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!amt_thread || !byte_ops || !byte_ops->export_name || !byte_ops->export_name->s)
		return;

	amtail_byteop *resolved = amtail_vm_make_temp_value(amt_thread);
	if (!resolved)
		return;

	char *resolved_key = NULL;
	if (byte_ops->metric_key_interpolate)
		resolved_key = amtail_vm_interpolate_metric_key(byte_ops->export_name->s, byte_ops->export_name->l, amt_thread, variables);
	char *lookup_key = resolved_key ? resolved_key : byte_ops->export_name->s;
	size_t lookup_len = strlen(lookup_key);
	amtail_lookup_key lk = { lookup_key, lookup_len };
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, amtail_hash(lookup_key, lookup_len));
	if (var)
	{
		resolved->vartype = var->type;
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			resolved->li = var->i;
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			resolved->ld = var->d;
		else if (var->type == ALLIGATOR_VARTYPE_TEXT && var->s && var->s->s)
			resolved->ls = string_string_init_dup(var->s);
		else if (var->type == ALLIGATOR_VARTYPE_CONST)
		{
			resolved->facttype = var->facttype;
			if (var->facttype == ALLIGATOR_FACTTYPE_INT)
			{
				resolved->vartype = ALLIGATOR_VARTYPE_COUNTER;
				resolved->li = var->i;
			}
			else if (var->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
			{
				resolved->vartype = ALLIGATOR_VARTYPE_GAUGE;
				resolved->ld = var->d;
			}
			else if (var->facttype == ALLIGATOR_FACTTYPE_TEXT && var->s && var->s->s)
			{
				resolved->vartype = ALLIGATOR_VARTYPE_TEXT;
				resolved->ls = string_string_init_dup(var->s);
			}
		}
		free(resolved_key);
		return;
	}
	free(resolved_key);

	{
		const char *emit = NULL;
		size_t emit_len = 0;
		if (amtail_vm_resolve_capture_token(byte_ops->export_name->s, byte_ops->export_name->l, amt_thread, variables, &emit, &emit_len) && emit_len)
		{
			resolved->vartype = ALLIGATOR_VARTYPE_TEXT;
			resolved->ls = string_init_alloc((char*)emit, emit_len);
			return;
		}
	}

	/* Fallback for inline literals emitted by parser (numeric or quoted string).
	 * The parser pushes operand tokens as VAR nodes whose export_name is the raw
	 * token text; numbers are looked up via byte_ops->li/ld, but for string and
	 * regex literals we need to strip the surrounding delimiters here so they
	 * reach function handlers (subst, tolower, strptime, ...) as TEXT values. */
	if (byte_ops->export_name && byte_ops->export_name->s)
	{
		char *s = byte_ops->export_name->s;
		size_t sl = byte_ops->export_name->l;
		if (sl >= 2 &&
		    ((s[0] == '"' && s[sl - 1] == '"') ||
		     (s[0] == '\'' && s[sl - 1] == '\'')))
		{
			resolved->vartype = ALLIGATOR_VARTYPE_TEXT;
			resolved->ls = string_init_alloc(s + 1, sl - 2);
			return;
		}
		/* /regex/ literal: keep the surrounding slashes so subst can detect it. */
		if (sl >= 2 && s[0] == '/' && s[sl - 1] == '/')
		{
			resolved->vartype = ALLIGATOR_VARTYPE_TEXT;
			resolved->ls = string_init_alloc(s, sl);
			return;
		}
	}

	resolved->vartype = byte_ops->vartype;
	if (byte_ops->vartype == ALLIGATOR_VARTYPE_COUNTER)
		resolved->li = byte_ops->li;
	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_GAUGE)
		resolved->ld = byte_ops->ld;
}

static int amtail_vm_branch_condition_true(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables)
{
	if (!byte_ops)
		return 0;

	/*
	 * Prefer already-evaluated condition from VM stack (e.g. EQ/NE result).
	 * This also consumes temporary booleans and prevents stack leakage.
	 */
	amtail_byteop *cond = amtail_vmstack_pop(amt_thread);
	if (cond)
	{
		double num = 0;
		if (amtail_vm_get_number(cond, &num))
		{
			int rc = num != 0;
			amtail_vm_free_tempop(cond);
			return rc;
		}
		char *s = amtail_vm_resolve_string(cond, variables, amt_thread);
		int rc = s && *s;
		free(s);
		amtail_vm_free_tempop(cond);
		return rc;
	}

	char *lhs = NULL, *rhs = NULL;
	if (byte_ops->export_name && byte_ops->export_name->s &&
	    amtail_vm_extract_binary_operands(byte_ops->export_name->s, &lhs, &rhs))
	{
		double ln = 0, rn = 0;
		int matched = 0;
		if (amtail_vm_token_to_number(lhs, variables, amt_thread, &ln) &&
		    amtail_vm_token_to_number(rhs, variables, amt_thread, &rn))
			matched = (ln == rn);
		else
		{
			char *ls = amtail_vm_token_to_string(lhs, variables, amt_thread);
			char *rs = amtail_vm_token_to_string(rhs, variables, amt_thread);
			matched = (ls && rs && strcmp(ls, rs) == 0);
			free(ls);
			free(rs);
		}
		free(lhs);
		free(rhs);
		return matched;
	}
	return 0;
}

uint64_t amtail_vmfunc_branch(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size, amtail_log_level amtail_ll)
{
	if (!byte_ops)
		return 0;

	if (!byte_ops->re_match)
	{
		int cond = amtail_vm_branch_condition_true(amt_thread, byte_ops, variables);
		return cond ? 0 : byte_ops->right_opcounter;
	}

	int ovector[240];
	int match = amtail_regex_exec_with_ovector(byte_ops->re_match, logline->s + offset, line_size, amtail_ll, ovector, 240);
	if (match)
		amtail_vm_apply_regex_captures(amt_thread, byte_ops->re_match, logline->s + offset, line_size, ovector, match);
	//fprintf(stderr, "branch pcre '%s' (jmp %"PRIu64", res: %"PRIu8" with logline '%p'\n", byte_ops->export_name->s+1, byte_ops->right_opcounter, match, logline->s);
	if (match)
		return 0;
	else
		return byte_ops->right_opcounter;
	//amtail_execute(byte_ops, variables, logline);
}

// TODO
void amtail_vmfunc_add(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->li = left->li + right->li;
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->ld = left->ld + right->li;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->li + right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->ld + right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
}

void amtail_vmfunc_sub(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->li = left->li - right->li;
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->ld = left->ld - right->li;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->li - right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->ld - right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
}

void amtail_vmfunc_mul(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{

	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->li = left->li * right->li;
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->ld = left->ld * right->li;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->li * right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->ld * right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
}

void amtail_vmfunc_pow(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{

	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->li = pow(left->li, right->li);
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->ld = pow(left->ld, right->li);
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = pow(left->li, right->ld);
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = pow(left->ld, right->ld);
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
}

void amtail_vmfunc_div(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{

	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->li = left->li / right->li;
		new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		new->ld = left->ld / right->li;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_COUNTER && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->li / right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
	else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE && right->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		new->ld = left->ld / right->ld;
		new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	}
}

void amtail_vmfunc_mod(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left || !right)
		return;

	double l = 0, r = 0;
	if (!amtail_vm_get_number(left, &l) || !amtail_vm_get_number(right, &r) || r == 0)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = fmod(l, r);
}

void amtail_vmfunc_cmp_lt(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l < r);
}

void amtail_vmfunc_cmp_le(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l <= r);
}

void amtail_vmfunc_cmp_gt(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l > r);
}

void amtail_vmfunc_cmp_ge(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l >= r);
}

void amtail_vmfunc_cmp_eq(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	char *lhs = NULL, *rhs = NULL;
	if (byte_ops && byte_ops->export_name && byte_ops->export_name->s &&
	    amtail_vm_extract_binary_operands(byte_ops->export_name->s, &lhs, &rhs))
	{
		double ln = 0, rn = 0;
		int result = 0;
		if (amtail_vm_token_to_number(lhs, variables, amt_thread, &ln) && amtail_vm_token_to_number(rhs, variables, amt_thread, &rn))
		{
			result = ln == rn;
		}
		else
		{
			char *ls = amtail_vm_token_to_string(lhs, variables, amt_thread);
			char *rs = amtail_vm_token_to_string(rhs, variables, amt_thread);
			result = (ls && rs && strcmp(ls, rs) == 0);
			free(ls);
			free(rs);
		}
		free(lhs);
		free(rhs);
		amtail_vm_push_bool(amt_thread, result);
		return;
	}

	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (left && right)
	{
		amtail_vm_push_bool(amt_thread, amtail_vm_compare_equal(left, right, variables, amt_thread));
		return;
	}

	amtail_vm_push_bool(amt_thread, 0);
}

void amtail_vmfunc_cmp_ne(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	char *lhs = NULL, *rhs = NULL;
	if (byte_ops && byte_ops->export_name && byte_ops->export_name->s &&
	    amtail_vm_extract_binary_operands(byte_ops->export_name->s, &lhs, &rhs))
	{
		double ln = 0, rn = 0;
		int result = 1;
		if (amtail_vm_token_to_number(lhs, variables, amt_thread, &ln) && amtail_vm_token_to_number(rhs, variables, amt_thread, &rn))
		{
			result = ln != rn;
		}
		else
		{
			char *ls = amtail_vm_token_to_string(lhs, variables, amt_thread);
			char *rs = amtail_vm_token_to_string(rhs, variables, amt_thread);
			result = !(ls && rs && strcmp(ls, rs) == 0);
			free(ls);
			free(rs);
		}
		free(lhs);
		free(rhs);
		amtail_vm_push_bool(amt_thread, result);
		return;
	}

	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (left && right)
	{
		amtail_vm_push_bool(amt_thread, !amtail_vm_compare_equal(left, right, variables, amt_thread));
		return;
	}

	amtail_vm_push_bool(amt_thread, 1);
}

void amtail_vmfunc_logic_and(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l != 0 && r != 0);
}

void amtail_vmfunc_logic_or(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && (l != 0 || r != 0));
}

void amtail_vmfunc_logic_not(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	double v = 0;
	amtail_vm_push_bool(amt_thread, !(val && amtail_vm_get_number(val, &v) && v != 0));
}

void amtail_vmfunc_match(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!amt_thread || !byte_ops || !byte_ops->re_match || !amt_thread->line_ptr)
	{
		amtail_vm_push_bool(amt_thread, 0);
		return;
	}

	int ovector[240];
	int matched = amtail_regex_exec_with_ovector(byte_ops->re_match, amt_thread->line_ptr, amt_thread->line_size, amtail_ll, ovector, 240);
	if (matched)
		amtail_vm_apply_regex_captures(amt_thread, byte_ops->re_match, amt_thread->line_ptr, amt_thread->line_size, ovector, matched);
	amtail_vm_push_bool(amt_thread, matched ? 1 : 0);
}

void amtail_vmfunc_notmatch(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!amt_thread || !byte_ops || !byte_ops->re_match || !amt_thread->line_ptr)
	{
		amtail_vm_push_bool(amt_thread, 1);
		return;
	}

	amtail_vm_push_bool(amt_thread, amtail_regex_exec(byte_ops->re_match, amt_thread->line_ptr, amt_thread->line_size, amtail_ll) ? 0 : 1);
}

void amtail_vmfunc_cast_int(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	int64_t iv = 0;
	int ok = amtail_vm_cast_to_int64(val, variables, amt_thread, &iv);
	amtail_vm_free_tempop(val);
	if (!ok)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = iv;
}

void amtail_vmfunc_cast_float(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;
	double v = 0;
	int ok = amtail_vm_cast_to_double(val, variables, amt_thread, &v);
	amtail_vm_free_tempop(val);
	if (!ok)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = v;
}

void amtail_vmfunc_cast_bool(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;
	double v = 0;
	int ok = amtail_vm_get_number(val, &v);
	amtail_vm_free_tempop(val);
	if (!ok)
		return;

	amtail_vm_push_bool(amt_thread, v != 0);
}

void amtail_vmfunc_cast_string(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables, amt_thread);
	amtail_vm_free_tempop(val);
	if (!s)
		return;

	amtail_vm_push_text(amt_thread, s);
	free(s);
}

void amtail_vmfunc_fn_strtol(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *base_op = amtail_vmstack_pop(amt_thread);
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
	{
		amtail_vm_free_tempop(base_op);
		return;
	}

	int64_t base = 10;
	if (base_op)
	{
		int64_t parsed_base = 0;
		if (amtail_vm_cast_to_int64(base_op, variables, amt_thread, &parsed_base) && parsed_base >= 2 && parsed_base <= 36)
			base = parsed_base;
		amtail_vm_free_tempop(base_op);
	}

	char *s = amtail_vm_resolve_string(val, variables, amt_thread);
	amtail_vm_free_tempop(val);
	if (!s)
		return;

	char *end = NULL;
	long long n = strtoll(s, &end, (int)base);
	int parse_ok = (end && *end == '\0');
	free(s);

	if (!parse_ok)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = (int64_t)n;
}

void amtail_vmfunc_fn_split(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *src_op = amtail_vmstack_pop(amt_thread);
	amtail_byteop *sep_op = amtail_vmstack_pop(amt_thread);
	if (!src_op)
	{
		amtail_vm_free_tempop(sep_op);
		return;
	}

	char *src = amtail_vm_resolve_string(src_op, variables, amt_thread);
	char *sep = sep_op ? amtail_vm_resolve_string(sep_op, variables, amt_thread) : NULL;
	amtail_vm_free_tempop(src_op);
	amtail_vm_free_tempop(sep_op);

	uint8_t n = 0;
	if (src && sep && *sep)
		n = amtail_vm_do_split(amt_thread, sep, strlen(sep), src, strlen(src));

	free(src);
	free(sep);

	if (n)
		amtail_vm_split_publish_captures(amt_thread);

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = (int64_t)n;
}

void amtail_vmfunc_range(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	(void)logline;
	(void)variables;
	amt_thread->split_active = 0;
	amt_thread->split_index = 0;
	amt_thread->split_loop_array = NULL;

	if (!byte_ops->ls || !byte_ops->ls->s || !byte_ops->rs || !byte_ops->rs->s)
		return;

	amtail_named_split_array *arr = amtail_vm_split_array_find(amt_thread, byte_ops->rs->s, byte_ops->rs->l);
	if (!arr || !arr->count)
		return;

	uint8_t bind_len = (uint8_t)byte_ops->ls->l;
	if (bind_len >= AMTAIL_SPLIT_BIND_MAX)
		bind_len = AMTAIL_SPLIT_BIND_MAX - 1;
	memcpy(amt_thread->split_bind, byte_ops->ls->s, bind_len);
	amt_thread->split_bind[bind_len] = '\0';
	amt_thread->split_bind_len = bind_len;

	amt_thread->split_loop_array = arr;
	amt_thread->split_active = 1;
	amt_thread->split_index = 0;
	amtail_vm_split_bind_current(amt_thread);
}

void amtail_vmfunc_range_step(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	(void)byte_ops;
	(void)variables;
	(void)logline;
	(void)amtail_ll;

	if (!amt_thread->split_active)
		return;

	++amt_thread->split_index;
	if (!amt_thread->split_loop_array || amt_thread->split_index >= amt_thread->split_loop_array->count)
	{
		amt_thread->split_active = 0;
		amt_thread->split_loop_array = NULL;
		return;
	}

	amtail_vm_split_bind_current(amt_thread);
}

void amtail_vmfunc_fn_len(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables, amt_thread);
	amtail_vm_free_tempop(val);
	if (!s)
		return;

	size_t l = strlen(s);
	free(s);

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = (int64_t)l;
}

void amtail_vmfunc_fn_tolower(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables, amt_thread);
	amtail_vm_free_tempop(val);
	if (!s)
		return;

	for (char *p = s; *p; ++p)
		*p = (char)tolower((unsigned char)*p);

	amtail_vm_push_text(amt_thread, s);
	free(s);
}

void amtail_vmfunc_fn_timestamp(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	/* Per mtail spec: undefined if neither settime/strptime have been called.
	 * Emit 0 in that case; settime/strptime establish the register. */
	if (amt_thread && amt_thread->timestamp_set)
		new->ld = amt_thread->timestamp_value;
	else
		new->ld = 0.0;
}

/* Translate Go time.Parse layout tokens to strptime(3) conversion specs so
 * callers can keep writing mtail scripts that follow the upstream Go syntax.
 * We only translate the handful of tokens the Go reference layout uses; any
 * unknown run is copied verbatim, which also means plain strftime-style specs
 * (e.g. %Y-%m-%d) pass through untouched. */
static char* amtail_vm_go_format_to_strptime(const char *go_fmt)
{
	if (!go_fmt)
		return NULL;

	size_t cap = strlen(go_fmt) * 2 + 4;
	char *out = calloc(1, cap);
	if (!out)
		return NULL;
	size_t oi = 0;

	struct { const char *token; const char *spec; } map[] = {
		{ "2006", "%Y" },
		{ "01",   "%m" },
		{ "02",   "%d" },
		{ "15",   "%H" },
		{ "04",   "%M" },
		{ "05",   "%S" },
		{ "Jan",  "%b" },
		{ "Mon",  "%a" },
		{ "PM",   "%p" },
	};

	const char *p = go_fmt;
	while (*p)
	{
		int matched = 0;
		for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i)
		{
			size_t tl = strlen(map[i].token);
			if (!strncmp(p, map[i].token, tl))
			{
				size_t sl = strlen(map[i].spec);
				if (oi + sl + 1 >= cap)
				{
					cap = (oi + sl + 1) * 2;
					out = realloc(out, cap);
					if (!out)
						return NULL;
				}
				memcpy(out + oi, map[i].spec, sl);
				oi += sl;
				p += tl;
				matched = 1;
				break;
			}
		}
		if (matched)
			continue;

		if (oi + 2 >= cap)
		{
			cap *= 2;
			out = realloc(out, cap);
			if (!out)
				return NULL;
		}
		out[oi++] = *p++;
	}
	out[oi] = '\0';
	return out;
}

static int amtail_vm_parse_with_format(const char *value, const char *fmt, double *out_epoch)
{
	if (!value || !fmt || !out_epoch)
		return 0;

	char *spec = amtail_vm_go_format_to_strptime(fmt);
	if (!spec)
		return 0;

	struct tm tmv;
	memset(&tmv, 0, sizeof(tmv));
	char *end = strptime(value, spec, &tmv);
	free(spec);
	if (!end)
		return 0;

	time_t t = mktime(&tmv);
	if (t < 0)
		return 0;

	*out_epoch = (double)t;
	return 1;
}

void amtail_vmfunc_fn_strptime(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	/* strptime(value, format) - pops 2 args: top is format, below is value.
	 * If only one arg was pushed, fall back to best-effort epoch parsing so
	 * legacy call sites keep working. The runcalc boundary marker (ASSIGN)
	 * stays on the stack, so push it back if we accidentally pop it. */
	amtail_byteop *fmt_op = amtail_vmstack_pop(amt_thread);
	if (!fmt_op)
		return;
	if (fmt_op->opcode == AMTAIL_AST_OPCODE_ASSIGN)
	{
		amtail_vmstack_push(amt_thread, fmt_op);
		return;
	}

	amtail_byteop *val_op = amtail_vmstack_pop(amt_thread);
	if (val_op && val_op->opcode == AMTAIL_AST_OPCODE_ASSIGN)
	{
		amtail_vmstack_push(amt_thread, val_op);
		val_op = NULL;
	}

	if (!val_op)
	{
		val_op = fmt_op;
		fmt_op = NULL;
	}

	char *value = amtail_vm_resolve_string(val_op, variables, amt_thread);
	char *fmt = fmt_op ? amtail_vm_resolve_string(fmt_op, variables, amt_thread) : NULL;
	amtail_vm_free_tempop(val_op);
	amtail_vm_free_tempop(fmt_op);

	if (!value)
	{
		free(fmt);
		return;
	}

	double epoch = 0;
	int ok = 0;
	if (fmt && *fmt)
		ok = amtail_vm_parse_with_format(value, fmt, &epoch);
	if (!ok)
		epoch = amtail_vm_parse_epoch_string(value);

	free(value);
	free(fmt);

	if (amt_thread && epoch > 0)
	{
		amt_thread->timestamp_set = 1;
		amt_thread->timestamp_value = epoch;
	}

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = epoch;
}

void amtail_vmfunc_fn_settime(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;
	if (val->opcode == AMTAIL_AST_OPCODE_ASSIGN)
	{
		amtail_vmstack_push(amt_thread, val);
		return;
	}

	double epoch = 0;
	int ok = amtail_vm_cast_to_double(val, variables, amt_thread, &epoch);
	if (!ok)
	{
		/* Allow "1709999999" style text to flow through. */
		char *s = amtail_vm_resolve_string(val, variables, amt_thread);
		if (s)
		{
			epoch = amtail_vm_parse_epoch_string(s);
			ok = epoch != 0;
			free(s);
		}
	}
	amtail_vm_free_tempop(val);

	if (!ok || !amt_thread)
		return;

	amt_thread->timestamp_set = 1;
	amt_thread->timestamp_value = epoch;

	/* settime is an expression statement on the mtail side; emit a placeholder
	 * gauge value so the surrounding RUN consumes something and the ASSIGN
	 * marker stays balanced. */
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = epoch;
}

void amtail_vmfunc_fn_getfilename(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	const char *name = (amt_thread && amt_thread->filename) ? amt_thread->filename : "";
	amtail_vm_push_text(amt_thread, name);
}

static char* amtail_vm_string_replace_all(const char *haystack, const char *needle, const char *replacement)
{
	if (!haystack)
		return NULL;
	if (!needle || !*needle)
		return strdup(haystack);
	if (!replacement)
		replacement = "";

	size_t nlen = strlen(needle);
	size_t rlen = strlen(replacement);

	size_t cap = strlen(haystack) + 1;
	char *out = calloc(1, cap);
	if (!out)
		return NULL;
	size_t oi = 0;

	const char *p = haystack;
	while (*p)
	{
		if (!strncmp(p, needle, nlen))
		{
			if (oi + rlen + 1 >= cap)
			{
				cap = (oi + rlen + 1) * 2;
				out = realloc(out, cap);
				if (!out)
					return NULL;
			}
			memcpy(out + oi, replacement, rlen);
			oi += rlen;
			p += nlen;
			continue;
		}

		if (oi + 2 >= cap)
		{
			cap *= 2;
			out = realloc(out, cap);
			if (!out)
				return NULL;
		}
		out[oi++] = *p++;
	}
	out[oi] = '\0';
	return out;
}

static char* amtail_vm_regex_replace_all(const char *haystack, const char *pattern, const char *replacement)
{
	if (!haystack || !pattern)
		return haystack ? strdup(haystack) : NULL;
	if (!replacement)
		replacement = "";

	regex_match *rm = amtail_regex_compile((char*)pattern);
	if (!rm || !rm->regex_compiled)
	{
		if (rm)
			amtail_regex_free(rm);
		return strdup(haystack);
	}

	size_t hlen = strlen(haystack);
	size_t rlen = strlen(replacement);
	size_t cap = hlen + 1;
	char *out = calloc(1, cap);
	if (!out)
	{
		amtail_regex_free(rm);
		return NULL;
	}
	size_t oi = 0;

	int offset = 0;
	int ovector[30];
	while ((size_t)offset <= hlen)
	{
		int rc = pcre_exec(rm->regex_compiled, rm->pcreExtra, haystack,
		                   (int)hlen, offset, 0, ovector, 30);
		if (rc < 0)
			break;

		int mstart = ovector[0];
		int mend = ovector[1];
		size_t pre = (size_t)(mstart - offset);

		if (oi + pre + rlen + 2 >= cap)
		{
			cap = (oi + pre + rlen + 2) * 2;
			out = realloc(out, cap);
			if (!out)
			{
				amtail_regex_free(rm);
				return NULL;
			}
		}

		memcpy(out + oi, haystack + offset, pre);
		oi += pre;

		memcpy(out + oi, replacement, rlen);
		oi += rlen;

		if (mend == mstart)
			++offset;
		else
			offset = mend;
	}

	size_t tail = hlen - (size_t)offset;
	if (oi + tail + 1 >= cap)
	{
		cap = oi + tail + 1;
		out = realloc(out, cap);
		if (!out)
		{
			amtail_regex_free(rm);
			return NULL;
		}
	}
	memcpy(out + oi, haystack + offset, tail);
	oi += tail;
	out[oi] = '\0';

	amtail_regex_free(rm);
	return out;
}

/* Pop a function argument while leaving the runcalc ASSIGN boundary marker
 * in place. Returns NULL if we ran into the marker or the stack is empty. */
static amtail_byteop* amtail_vm_pop_arg(amtail_thread *amt_thread)
{
	amtail_byteop *op = amtail_vmstack_pop(amt_thread);
	if (!op)
		return NULL;
	if (op->opcode == AMTAIL_AST_OPCODE_ASSIGN)
	{
		amtail_vmstack_push(amt_thread, op);
		return NULL;
	}
	return op;
}

void amtail_vmfunc_fn_subst(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	/* subst(old, new, val) - args pushed left-to-right so val is on top. */
	amtail_byteop *val_op = amtail_vm_pop_arg(amt_thread);
	amtail_byteop *new_op = val_op ? amtail_vm_pop_arg(amt_thread) : NULL;
	amtail_byteop *old_op = new_op ? amtail_vm_pop_arg(amt_thread) : NULL;
	if (!val_op || !new_op || !old_op)
	{
		amtail_vm_free_tempop(val_op);
		amtail_vm_free_tempop(new_op);
		amtail_vm_free_tempop(old_op);
		return;
	}

	char *val = amtail_vm_resolve_string(val_op, variables, amt_thread);
	char *newstr = amtail_vm_resolve_string(new_op, variables, amt_thread);
	char *oldstr = amtail_vm_resolve_string(old_op, variables, amt_thread);
	amtail_vm_free_tempop(val_op);
	amtail_vm_free_tempop(new_op);
	amtail_vm_free_tempop(old_op);

	if (!val || !newstr || !oldstr)
	{
		free(val);
		free(newstr);
		free(oldstr);
		return;
	}

	size_t ol = strlen(oldstr);
	int is_regex = (ol >= 2 && oldstr[0] == '/' && oldstr[ol - 1] == '/');
	char *result = NULL;
	if (is_regex)
	{
		char *pattern = strndup(oldstr + 1, ol - 2);
		result = amtail_vm_regex_replace_all(val, pattern, newstr);
		free(pattern);
	}
	else
	{
		result = amtail_vm_string_replace_all(val, oldstr, newstr);
	}

	free(val);
	free(newstr);
	free(oldstr);

	if (!result)
		return;

	amtail_vm_push_text(amt_thread, result);
	free(result);
}

void amtail_vmfunc_runcalc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	if (!left)
	{
		amtail_vm_stack_clear(amt_thread);
		return;
	}

	/*
	 * Robustly locate the assignment target marker on stack.
	 * The VM may carry temporary values between ops, so don't assume
	 * fixed [value, assign] layout.
	 */
	amtail_byteop *right = NULL;
	amtail_byteop *candidate = NULL;
	while ((candidate = amtail_vmstack_pop(amt_thread)))
	{
		if (candidate->opcode == AMTAIL_AST_OPCODE_ASSIGN)
		{
			right = candidate;
			break;
		}
		amtail_vm_free_tempop(candidate);
	}

	if (!right || !right->export_name || !right->export_name->s)
	{
		amtail_vm_free_tempop(left);
		amtail_vm_stack_clear(amt_thread);
		return;
	}

	char *resolved_key = NULL;
	if (right->metric_key_interpolate)
		resolved_key = amtail_vm_interpolate_metric_key(right->export_name->s, right->export_name->l, amt_thread, variables);
	const char *lookup_key = resolved_key ? resolved_key : right->export_name->s;
	size_t lookup_len = strlen(lookup_key);
	amtail_lookup_key lk = { lookup_key, lookup_len };
	uint32_t name_hash = amtail_hash((char*)lookup_key, lookup_len);
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, name_hash);
	if (!var)
	{
		/* Implicit vars created by assignment should be local (hidden)
		 * unless they map to an explicitly declared metric/template. */
		uint8_t hidden = 1;
		uint8_t vartype = left->vartype;
		char *key = strdup(lookup_key);
		if (!key)
		{
			free(resolved_key);
			amtail_vm_free_tempop(left);
			amtail_vm_stack_clear(amt_thread);
			return;
		}

		char template_name[255];
		uint8_t template_size = strcspn(right->export_name->s, "[");
		strlcpy(template_name, right->export_name->s, template_size + 1);
		amtail_lookup_key tmplk = { template_name, template_size };
		amtail_variable *template_var = alligator_ht_search_nolock(variables, amtail_variable_compare, &tmplk, amtail_hash(template_name, template_size));
		if (template_var)
		{
			vartype = template_var->type;
			hidden = template_var->hidden;
		}

		string *new_export_name = string_new();
		string_cat(new_export_name, template_name, template_size);

		uint8_t *by_positions = NULL;
		string **by = NULL;
		uint8_t by_count = 0;
		string **bucket = NULL;
		uint8_t bucket_count = 0;

		if (template_var && template_var->by && template_var->by_count) {
			char *ptrby = key + template_size;
			by_positions = malloc(sizeof(*by_positions) * (template_var->by_count + 1));
			uint8_t i = 0;
			for (i = 0; i < template_var->by_count; ++i)
			{
				ptrby = strstr(ptrby, "[");
				if (!ptrby)
					break;

				by_positions[i] = ++ptrby - key;
			}

			ptrby = strstr(ptrby, "]");
			if (!ptrby)
				by_positions[i] = right->export_name->l;
			else
				by_positions[i] = ptrby - key + 2;

			by = template_var->by;
			by_count = template_var->by_count;
		}

		var = amtail_variable_make(hidden, vartype, key, new_export_name, by, by_count, by_positions);
		free(key);
		if (template_var && template_var->bucket && template_var->bucket_count)
		{
			bucket = template_var->bucket;
			bucket_count = template_var->bucket_count;
		}
		var->bucket = bucket;
		var->bucket_count = bucket_count;
		if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
			amtail_histogram_init(var);
		alligator_ht_insert_nolock(variables, &(var->node), var, name_hash);
		if (!var->is_template && !var->hidden)
			amtail_invoke_var_touched(amt_thread, var);
	}
	free(resolved_key);

	if (amt_thread->split_count > 0 && strchr(lookup_key, '[') == NULL)
	{
		size_t nl = strcspn(lookup_key, "[");
		const char *store_name = lookup_key;
		if (nl > 0)
		{
			if (store_name[0] == '$')
			{
				++store_name;
				--nl;
			}
			amtail_vm_split_array_commit_from_global(amt_thread, store_name, nl);
		}
	}

	if (var->type == ALLIGATOR_VARTYPE_COUNTER)
	{
		int64_t iv = 0;
		if (amtail_vm_cast_to_int64(left, variables, amt_thread, &iv))
		{
			var->i = iv;
			amtail_invoke_var_touched(amt_thread, var);
			if (amtail_ll.vm > 1)
				fprintf(stderr, "load variable %s/%s: c/* %"PRId64"\n", var->export_name->s, var->key->s, var->i);
		}
	}
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
	{
		double dv = 0;
		if (amtail_vm_cast_to_double(left, variables, amt_thread, &dv))
		{
			var->d = dv;
			amtail_invoke_var_touched(amt_thread, var);
			if (amtail_ll.vm > 1) {
				fprintf(stderr, "load variable %s/%s: g/* %lf, by %p(%"PRIu8")\n", var->export_name->s, var->key->s, var->d, var->by, var->by_count);
				if (var->by && var->by_count)
				{
					for (uint64_t i = 0; i < var->by_count; ++i)
						printf("\t\tby[%"PRIu64"] %s\n", i, var->by[i]->s);
				}
			}
		}
	}
	else if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
	{
		double hv = 0;
		if (amtail_vm_cast_to_double(left, variables, amt_thread, &hv))
		{
			amtail_histogram_observe(var, hv);
			amtail_invoke_var_touched(amt_thread, var);
		}
	}
	else if (var->type == ALLIGATOR_VARTYPE_TEXT && left->vartype == ALLIGATOR_VARTYPE_TEXT &&
	         left->ls && left->ls->s)
	{
		size_t nl = left->ls->l;
		if (var->s && var->s->m >= nl + 1u)
		{
			memcpy(var->s->s, left->ls->s, nl);
			var->s->s[nl] = '\0';
			var->s->l = nl;
		}
		else
		{
			if (var->s)
				string_free(var->s);
			var->s = string_string_init_dup(left->ls);
		}
		amtail_invoke_var_touched(amt_thread, var);
		if (amtail_ll.vm > 1)
			fprintf(stderr, "load variable %s/%s: t/t '%s'\n", var->export_name->s, var->key->s, var->s->s);
	}

	amtail_vm_free_tempop(left);

	/* RUN terminates expression evaluation; keep stack isolated per expression. */
	amtail_vm_stack_clear(amt_thread);
}
// TODO end

/* Resolve metric key (with optional `[$…]` interpolation) and find or create instance from template. */
static amtail_variable *amtail_vm_lookup_or_create_metric(amtail_thread *amt_thread, const char *raw_name, size_t raw_len,
	uint8_t interpolate, alligator_ht *variables)
{
	char *resolved_key = NULL;
	const char *lookup_key = raw_name;
	size_t lookup_len = raw_len;

	if (!lookup_key || !lookup_len)
		return NULL;

	if (interpolate)
	{
		resolved_key = amtail_vm_interpolate_metric_key(raw_name, raw_len, amt_thread, variables);
		if (resolved_key)
		{
			lookup_key = resolved_key;
			lookup_len = strlen(resolved_key);
		}
	}

	uint32_t name_hash = amtail_hash((char*)lookup_key, lookup_len);
	amtail_lookup_key lk = { lookup_key, lookup_len };
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, name_hash);
	if (var)
	{
		free(resolved_key);
		return var;
	}

	size_t template_size = strcspn(lookup_key, "[");
	if (template_size >= lookup_len)
	{
		free(resolved_key);
		return NULL;
	}

	char template_name[255];
	if (template_size >= sizeof(template_name))
		template_size = sizeof(template_name) - 1;
	memcpy(template_name, lookup_key, template_size);
	template_name[template_size] = '\0';

	amtail_lookup_key tmplk = { template_name, template_size };
	amtail_variable *template_var = alligator_ht_search_nolock(variables, amtail_variable_compare, &tmplk,
		amtail_hash(template_name, template_size));
	if (!template_var)
	{
		free(resolved_key);
		return NULL;
	}

	char *key = strndup(lookup_key, lookup_len);
	if (!key)
	{
		free(resolved_key);
		return NULL;
	}

	string *new_export_name = string_new();
	string_cat(new_export_name, template_name, template_size);

	uint8_t *by_positions = NULL;
	string **by = NULL;
	uint8_t by_count = 0;

	if (template_var->by && template_var->by_count)
	{
		char *ptrby = key + template_size;
		by_positions = malloc(sizeof(*by_positions) * (template_var->by_count + 1));
		if (!by_positions)
		{
			string_free(new_export_name);
			free(key);
			free(resolved_key);
			return NULL;
		}
		uint8_t i = 0;
		for (i = 0; i < template_var->by_count; ++i)
		{
			ptrby = strstr(ptrby, "[");
			if (!ptrby)
				break;
			by_positions[i] = ++ptrby - key;
		}
		ptrby = strstr(ptrby, "]");
		if (!ptrby)
			by_positions[i] = lookup_len;
		else
			by_positions[i] = ptrby - key + 2;
		by = template_var->by;
		by_count = template_var->by_count;
	}

	var = amtail_variable_make(template_var->hidden, template_var->type, key, new_export_name, by, by_count, by_positions);
	free(key);
	if (template_var->bucket && template_var->bucket_count)
	{
		var->bucket = template_var->bucket;
		var->bucket_count = template_var->bucket_count;
	}
	if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
		amtail_histogram_init(var);
	alligator_ht_insert_nolock(variables, &(var->node), var, name_hash);
	if (!var->is_template && !var->hidden)
		amtail_invoke_var_touched(amt_thread, var);
	free(resolved_key);
	return var;
}

void amtail_vmfunc_inc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!byte_ops || !byte_ops->export_name || !byte_ops->export_name->s)
		return;

	amtail_variable *var = NULL;
	if (byte_ops->metric_key_interpolate)
		var = amtail_vm_lookup_or_create_metric(amt_thread, byte_ops->export_name->s, byte_ops->export_name->l,
			byte_ops->metric_key_interpolate, variables);
	else
	{
		amtail_lookup_key lk = { byte_ops->export_name->s, byte_ops->export_name->l };
		var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk,
			amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	}

	if (!var || var->is_template)
		return;

	if (var->type == ALLIGATOR_VARTYPE_COUNTER)
		++var->i;
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
		++var->d;
	amtail_invoke_var_touched(amt_thread, var);
}

void amtail_vmfunc_dec(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!byte_ops || !byte_ops->export_name || !byte_ops->export_name->s)
		return;

	amtail_variable *var = NULL;
	if (byte_ops->metric_key_interpolate)
		var = amtail_vm_lookup_or_create_metric(amt_thread, byte_ops->export_name->s, byte_ops->export_name->l,
			byte_ops->metric_key_interpolate, variables);
	else
	{
		amtail_lookup_key lk = { byte_ops->export_name->s, byte_ops->export_name->l };
		var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk,
			amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	}

	if (!var || var->is_template)
		return;

	if (var->type == ALLIGATOR_VARTYPE_COUNTER)
		--var->i;
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
		--var->d;
	amtail_invoke_var_touched(amt_thread, var);
}

void amtail_vmfunc_variable(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	uint32_t name_hash = amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l);
	amtail_lookup_key lk = { byte_ops->export_name->s, byte_ops->export_name->l };
	amtail_variable *var = alligator_ht_search_nolock(variables, amtail_variable_compare, &lk, name_hash);
	if (!var)
	{
		var = calloc(1, sizeof(*var));
		var->hidden = byte_ops->hidden;
		var->type = byte_ops->vartype;
		var->key = string_string_init_dup(byte_ops->export_name);
		var->export_name = string_new();

		if (byte_ops->by_count)
		{
			var->by_count = byte_ops->by_count;
			var->by = byte_ops->by;
			var->is_template = 1;
		}
		if (byte_ops->bucket_count)
		{
			var->bucket_count = byte_ops->bucket_count;
			var->bucket = byte_ops->bucket;
		}

		string_string_cat(var->export_name, byte_ops->export_name);
		if (var->type == ALLIGATOR_VARTYPE_CONST)
		{
			var->facttype = byte_ops->facttype;
			if (byte_ops->facttype == ALLIGATOR_FACTTYPE_INT)
				var->i = byte_ops->li;
			else if (byte_ops->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
				var->d = byte_ops->ld;
			else if (byte_ops->facttype == ALLIGATOR_FACTTYPE_TEXT && byte_ops->ls && byte_ops->ls->s)
				var->s = string_string_init_dup(byte_ops->ls);
		}
		if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM && !var->is_template)
			amtail_histogram_init(var);
		if (amtail_ll.vm > 1)
			printf("create variable %s with type %hhu and pointer: %p\n", byte_ops->export_name->s, byte_ops->vartype, var);
		alligator_ht_insert_nolock(variables, &(var->node), var, name_hash);
		if (!var->is_template && !var->hidden)
			amtail_invoke_var_touched(amt_thread, var);
	}
	else
		if (amtail_ll.vm > 0)
			fprintf(stderr, "error: variable called as '%s' already declared\n", byte_ops->export_name->s);
}

void amtail_vmfunc_noop(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
}

void amtail_vm_init()
{
	amtail_vmfunc[AMTAIL_AST_OPCODE_NOOP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_VARIABLE] = amtail_vmfunc_variable;
	//amtail_vmfunc[AMTAIL_AST_OPCODE_BRANCH] = amtail_vmfunc_branch;
	amtail_vmfunc[AMTAIL_AST_OPCODE_INC] = amtail_vmfunc_inc;
	amtail_vmfunc[AMTAIL_AST_OPCODE_DEC] = amtail_vmfunc_dec;
	amtail_vmfunc[AMTAIL_AST_OPCODE_ADD] = amtail_vmfunc_add;
	amtail_vmfunc[AMTAIL_AST_OPCODE_SUB] = amtail_vmfunc_sub;
	amtail_vmfunc[AMTAIL_AST_OPCODE_MUL] = amtail_vmfunc_mul;
	amtail_vmfunc[AMTAIL_AST_OPCODE_POW] = amtail_vmfunc_pow;
	amtail_vmfunc[AMTAIL_AST_OPCODE_DIV] = amtail_vmfunc_div;
	amtail_vmfunc[AMTAIL_AST_OPCODE_MOD] = amtail_vmfunc_mod;
	amtail_vmfunc[AMTAIL_AST_OPCODE_LT] = amtail_vmfunc_cmp_lt;
	amtail_vmfunc[AMTAIL_AST_OPCODE_LE] = amtail_vmfunc_cmp_le;
	amtail_vmfunc[AMTAIL_AST_OPCODE_GT] = amtail_vmfunc_cmp_gt;
	amtail_vmfunc[AMTAIL_AST_OPCODE_GE] = amtail_vmfunc_cmp_ge;
	amtail_vmfunc[AMTAIL_AST_OPCODE_EQ] = amtail_vmfunc_cmp_eq;
	amtail_vmfunc[AMTAIL_AST_OPCODE_NE] = amtail_vmfunc_cmp_ne;
	amtail_vmfunc[AMTAIL_AST_OPCODE_AND] = amtail_vmfunc_logic_and;
	amtail_vmfunc[AMTAIL_AST_OPCODE_OR] = amtail_vmfunc_logic_or;
	amtail_vmfunc[AMTAIL_AST_OPCODE_NOT] = amtail_vmfunc_logic_not;
	amtail_vmfunc[AMTAIL_AST_OPCODE_MATCH] = amtail_vmfunc_match;
	amtail_vmfunc[AMTAIL_AST_OPCODE_NOTMATCH] = amtail_vmfunc_notmatch;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_DECLARATION] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_CALL] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_STOP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_MATCH] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_CMP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_JMP] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_STRPTIME] = amtail_vmfunc_fn_strptime;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_TIMESTAMP] = amtail_vmfunc_fn_timestamp;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_TOLOWER] = amtail_vmfunc_fn_tolower;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_LEN] = amtail_vmfunc_fn_len;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_STRTOL] = amtail_vmfunc_fn_strtol;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_SETTIME] = amtail_vmfunc_fn_settime;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_GETFILENAME] = amtail_vmfunc_fn_getfilename;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_INT] = amtail_vmfunc_cast_int;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_BOOL] = amtail_vmfunc_cast_bool;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_FLOAT] = amtail_vmfunc_cast_float;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_STRING] = amtail_vmfunc_cast_string;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_SUBST] = amtail_vmfunc_fn_subst;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_SPLIT] = amtail_vmfunc_fn_split;
	amtail_vmfunc[AMTAIL_AST_OPCODE_RANGE] = amtail_vmfunc_range;
	amtail_vmfunc[AMTAIL_AST_OPCODE_RANGE_STEP] = amtail_vmfunc_range_step;
	amtail_vmfunc[AMTAIL_AST_OPCODE_ASSIGN] = amtail_vmfunc_assign;
	amtail_vmfunc[AMTAIL_AST_OPCODE_VAR] = amtail_vmfunc_var_use;
	amtail_vmfunc[AMTAIL_AST_OPCODE_RUN] = amtail_vmfunc_runcalc;
}

amtail_thread* amtail_thread_init()
{
	amtail_thread *amt_thread = calloc(1, sizeof(*amt_thread));
	return amt_thread;
}

void amtail_thread_free(amtail_thread *amt_thread)
{
	if (!amt_thread)
		return;
	amtail_vm_split_arrays_reset(amt_thread);
	free(amt_thread->split_storage);
	free(amt_thread);
}

int amtail_pre_execute(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH) // branch
		return 2;
	else if 
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	
	{
		//printf("byte_ops %p\n", byte_ops);
		amtail_vmfunc[byte_ops->opcode](amt_thread, byte_ops, variables, logline, amtail_ll);
	}
	else
	{
		return 0;
	}
	return 1;
}

uint64_t amtail_branch_select(amtail_thread *amt_thread, amtail_bytecode *byte_code, uint64_t opindex, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size, amtail_log_level amtail_ll)
{
	amtail_byteop *byte_ops = &byte_code->ops[opindex];
	if (byte_ops->re_match)
		return amtail_vmfunc_branch(amt_thread, byte_ops, variables, logline, offset, line_size, amtail_ll);

	/*
	 * Non-regex branches (e.g. `if (lhs OP rhs) { ... }`):
	 * - If condition is true, fall through to LEFT subtree (body).
	 * - If false, jump to `right_opcounter`, which the code generator set
	 *   to the last opcode of LEFT subtree (so main loop's `++i` lands on
	 *   the first opcode of RIGHT subtree, i.e. the next sibling stmt).
	 *
	 * This relies on the parser producing a well-formed AST where the
	 * block body hangs off BRANCH.LEFT and subsequent siblings are on
	 * BRANCH.RIGHT (see parser.c `}` handler).
	 */
	int cond = amtail_vm_branch_condition_true(amt_thread, byte_ops, variables);
	return cond ? 0 : byte_ops->right_opcounter;
}

int amtail_execute(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH)
		return 2;
	else if (byte_ops->opcode == AMTAIL_AST_OPCODE_RANGE)
	{
		amtail_vmfunc_range(amt_thread, byte_ops, variables, logline, amtail_ll);
		if (amt_thread->split_active && byte_ops->right_opcounter)
			return 3;
		return 1;
	}
	else if (byte_ops->opcode == AMTAIL_AST_OPCODE_RANGE_STEP)
	{
		amtail_vmfunc_range_step(amt_thread, byte_ops, variables, logline, amtail_ll);
		if (amt_thread->split_active && byte_ops->li)
			return 4;
		return 1;
	}
	else if (byte_ops->opcode == AMTAIL_AST_OPCODE_VARIABLE)
		return 1;
	else if (
		(byte_ops->opcode == AMTAIL_AST_OPCODE_INC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_DEC) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_ADD) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_SUB) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_MUL) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_POW) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_DIV) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_MOD) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_LT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_LE) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_GT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_GE) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_EQ) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NE) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_AND) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_OR) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NOT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_MATCH) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NOTMATCH) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_DECLARATION) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_CALL) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_STOP) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_MATCH) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_CMP) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_JMP) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_STRPTIME) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_TIMESTAMP) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_TOLOWER) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_LEN) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_STRTOL) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_SETTIME) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_GETFILENAME) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_INT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_BOOL) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_FLOAT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_STRING) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_SUBST) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_FUNC_SPLIT) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_ASSIGN) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_VAR) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_RUN) ||
		(byte_ops->opcode == AMTAIL_AST_OPCODE_NOOP)
	)
	{
		//printf("\nbyte_ops %p code %d\n", byte_ops, byte_ops->opcode);
		amtail_vmfunc[byte_ops->opcode](amt_thread, byte_ops, variables, logline, amtail_ll);
	}
	else
	{
		printf("NOT defined OP: %d\n", byte_ops->opcode);
		return 0;
	}
	return 1;
}

void amtail_bytecode_dump(amtail_bytecode* byte_code)
{
	uint64_t size = byte_code->l;

	for (uint64_t i = 0; i < size; ++i)
	{
		printf("[%"PRIu64"] opcode %d (%s)\n", i, byte_code->ops[i].opcode, opname_from_code(byte_code->ops[i].opcode));
		printf("\t[%"PRIu64"] jmp %"PRIu64"\n", i, byte_code->ops[i].right_opcounter);
		printf("\t[%"PRIu64"] re_match %p\n", i, byte_code->ops[i].re_match);
		printf("\t[%"PRIu64"] hidden %d\n", i, byte_code->ops[i].hidden);
		if (byte_code->ops[i].export_name)
			printf("\t[%"PRIu64"] name '%s'\n", i, byte_code->ops[i].export_name->s);
		if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VARIABLE && byte_code->ops[i].by_count) {
			for (uint8_t j = 0; j < byte_code->ops[i].by_count; ++j)
				printf("\t\tby [%"PRIu64"] var label %s\n", i, byte_code->ops[i].by[j]->s);
		}
		if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VARIABLE && byte_code->ops[i].bucket_count) {
			for (uint8_t j = 0; j < byte_code->ops[i].bucket_count; ++j)
				printf("\t\tbucket [%"PRIu64"] var label %s\n", i, byte_code->ops[i].bucket[j]->s);
		}
		if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VARIABLE && byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_CONST) {
			if (byte_code->ops[i].facttype == ALLIGATOR_FACTTYPE_INT)
				printf("\t[%"PRIu64"] const int value: %"PRId64"\n", i, byte_code->ops[i].li);
			else if (byte_code->ops[i].facttype == ALLIGATOR_FACTTYPE_DOUBLE)
			printf("\t[%"PRIu64"] const double value: %f\n", i, byte_code->ops[i].ld);
			else if (byte_code->ops[i].facttype == ALLIGATOR_FACTTYPE_TEXT)
				printf("\t[%"PRIu64"] const text value: %s\n", i, byte_code->ops[i].ls->s);
	
		}
		if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VAR && byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_COUNTER)
		{
			printf("\t[%"PRIu64"] var counter left %"PRId64"\n", i, byte_code->ops[i].li);
			printf("\t[%"PRIu64"] var counter right %"PRId64"\n", i, byte_code->ops[i].ri);
		}
		else if (byte_code->ops[i].opcode == AMTAIL_AST_OPCODE_VAR && byte_code->ops[i].vartype == ALLIGATOR_VARTYPE_GAUGE)
		{
			printf("\t[%"PRIu64"] var gauge left %lf\n", i, byte_code->ops[i].ld);
			printf("\t[%"PRIu64"] var gauge right %lf\n", i, byte_code->ops[i].rd);
		}
	}
}

int amtail_run_file(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, const char *filename, amtail_log_level amtail_ll, const amtail_touch_callbacks *touch, struct amtail_thread *reuse_thread)
{
	uint64_t size = byte_code->l;
	amtail_byteop *byte_ops = byte_code->ops;
	int rc;
	uint64_t line_size = 0;
	int owns_thread = 0;

	amtail_thread *amt_thread = reuse_thread;
	if (!amt_thread)
	{
		amt_thread = amtail_thread_init();
		owns_thread = 1;
	}
	amt_thread->filename = filename;
	if (touch)
		amt_thread->touch = *touch;
	else
		memset(&amt_thread->touch, 0, sizeof(amt_thread->touch));

	if (!byte_code->prepared)
	{
		for (uint64_t i = 0; i < size; ++i)
		{
			amtail_pre_execute(amt_thread, &byte_ops[i], variables, logline, amtail_ll);
		}
		byte_code->prepared = 1;
	}

	for (uint64_t cursym_log = 0; cursym_log < logline->l; )
	{
		line_size = strcspn(logline->s + cursym_log, "\n");
		amt_thread->line_ptr = logline->s + cursym_log;
		amt_thread->line_size = line_size;
		/* Per-line reset of the timestamp register: the spec defines it as
		 * line-scoped (established by settime/strptime during line processing). */
		amt_thread->timestamp_set = 0;
		amt_thread->timestamp_value = 0;
		amtail_vm_captures_reset(amt_thread);
		amtail_vm_stack_clear(amt_thread);
		for (uint64_t i = 0; i < size; ++i)
		{
			rc = amtail_execute(amt_thread, &byte_ops[i], variables, logline, amtail_ll);
			if (rc == 2) // branch
			{
				uint64_t new = amtail_branch_select(amt_thread, byte_code, i, variables, logline, cursym_log, line_size, amtail_ll);
				if (new)
					i = new;
			}
			else if (rc == 3) // split-foreach: enter body
			{
				uint64_t body = byte_ops[i].right_opcounter;
				if (body)
					i = body - 1;
			}
			else if (rc == 4) // split-foreach: next iteration
			{
				uint64_t body = byte_ops[i].li;
				if (body)
					i = body - 1;
			}
			else if (!rc)
			{
				printf("error execute on logline: '%s', %d\n", logline->s, rc);
				amtail_vm_stack_clear(amt_thread);
				memset(&amt_thread->touch, 0, sizeof(amt_thread->touch));
				if (owns_thread)
					amtail_thread_free(amt_thread);
				return rc;
			}
		}

		cursym_log += line_size;
		cursym_log += strspn(logline->s + cursym_log, "\n");
	}

	amtail_vm_stack_clear(amt_thread);
	memset(&amt_thread->touch, 0, sizeof(amt_thread->touch));
	if (owns_thread)
		amtail_thread_free(amt_thread);
	return 1;
}

int amtail_run(amtail_bytecode* byte_code, alligator_ht *variables, string* logline, amtail_log_level amtail_ll, const amtail_touch_callbacks *touch, struct amtail_thread *reuse_thread)
{
	return amtail_run_file(byte_code, variables, logline, NULL, amtail_ll, touch, reuse_thread);
}
