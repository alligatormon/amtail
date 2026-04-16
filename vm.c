#include "common/selector.h"
#include "generator.h"
#include "amtail_pcre.h"
#include "vm.h"
#include "variables.h"
#include "dstructures/tommy.h"
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>

void (*amtail_vmfunc[256])(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll);
void amtail_vmstack_push(amtail_thread *amt_thread, amtail_byteop *byte_ops);

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
}

static amtail_byteop* amtail_vm_make_temp_value(amtail_thread *amt_thread)
{
	amtail_byteop *new = calloc(1, sizeof(*new));
	if (!new)
		return NULL;
	new->allocated = 1;
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

static char* amtail_vm_resolve_string(amtail_byteop *op, alligator_ht *variables)
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
		if (variables)
		{
			amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare,
			                                           op->export_name->s,
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

	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare,
	                                           byte_ops->export_name->s,
	                                           amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
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
		return;
	}

	/* Fallback for inline numeric literals emitted by parser. */
	resolved->vartype = byte_ops->vartype;
	if (byte_ops->vartype == ALLIGATOR_VARTYPE_COUNTER)
		resolved->li = byte_ops->li;
	else if (byte_ops->vartype == ALLIGATOR_VARTYPE_GAUGE)
		resolved->ld = byte_ops->ld;
}

uint64_t amtail_vmfunc_branch(amtail_byteop *byte_ops, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size, amtail_log_level amtail_ll)
{
	if (!byte_ops || !byte_ops->re_match || !logline || !logline->s)
		return byte_ops ? byte_ops->right_opcounter : 0;

	uint8_t match = amtail_regex_exec(byte_ops->re_match, logline->s+offset, line_size, amtail_ll);
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
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l == r);
}

void amtail_vmfunc_cmp_ne(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	double l = 0, r = 0;
	amtail_vm_push_bool(amt_thread, left && right && amtail_vm_get_number(left, &l) && amtail_vm_get_number(right, &r) && l != r);
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

	amtail_vm_push_bool(amt_thread, amtail_regex_exec(byte_ops->re_match, amt_thread->line_ptr, amt_thread->line_size, amtail_ll) ? 1 : 0);
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
	double v = 0;
	if (!amtail_vm_get_number(val, &v))
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = (int64_t)v;
}

void amtail_vmfunc_cast_float(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;
	double v = 0;
	if (!amtail_vm_get_number(val, &v))
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
	if (!amtail_vm_get_number(val, &v))
		return;

	amtail_vm_push_bool(amt_thread, v != 0);
}

void amtail_vmfunc_fn_strtol(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables);
	if (!s)
		return;

	char *end = NULL;
	long long n = strtoll(s, &end, 10);
	free(s);

	if (!end)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_COUNTER;
	new->li = (int64_t)n;
}

void amtail_vmfunc_fn_len(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables);
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

	char *s = amtail_vm_resolve_string(val, variables);
	if (!s)
		return;

	for (char *p = s; *p; ++p)
		*p = (char)tolower((unsigned char)*p);

	amtail_vm_push_text(amt_thread, s);
	free(s);
}

void amtail_vmfunc_fn_timestamp(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return;

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);
}

void amtail_vmfunc_fn_strptime(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	/*
	 * Parser does not yet preserve full call argument structure.
	 * For now: parse top stack value as epoch-compatible string/number.
	 */
	amtail_byteop *val = amtail_vmstack_pop(amt_thread);
	if (!val)
		return;

	char *s = amtail_vm_resolve_string(val, variables);
	if (!s)
		return;
	double epoch = amtail_vm_parse_epoch_string(s);
	free(s);

	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;
	new->vartype = ALLIGATOR_VARTYPE_GAUGE;
	new->ld = epoch;
}

void amtail_vmfunc_runcalc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	amtail_byteop *left = amtail_vmstack_pop(amt_thread);
	amtail_byteop *right = amtail_vmstack_pop(amt_thread);
	if (!left || !right || !right->export_name || !right->export_name->s)
		return;
	amtail_byteop *new = amtail_vm_make_temp_value(amt_thread);
	if (!new)
		return;

	uint32_t name_hash = amtail_hash(right->export_name->s, right->export_name->l);
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, right->export_name->s, name_hash);
	if (!var)
	{
		uint8_t hidden = 0; // TODO
		uint8_t vartype = left->vartype;
		char *key = strdup(right->export_name->s);

		char template_name[255];
		uint8_t template_size = strcspn(right->export_name->s, "[");
		strlcpy(template_name, right->export_name->s, template_size + 1);
		amtail_variable *template_var = alligator_ht_search(variables, amtail_variable_compare, template_name, amtail_hash(template_name, template_size));
		if (template_var)
			vartype = template_var->type;

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
		if (template_var && template_var->bucket && template_var->bucket_count)
		{
			bucket = template_var->bucket;
			bucket_count = template_var->bucket_count;
		}
		var->bucket = bucket;
		var->bucket_count = bucket_count;
		if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
			amtail_histogram_init(var);
		alligator_ht_insert(variables, &(var->node), var, name_hash);
	}

	if (var->type == ALLIGATOR_VARTYPE_COUNTER && left->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		var->i = left->li;
		if (amtail_ll.vm > 1)
			fprintf(stderr, "load variable %s/%s: c/c %"PRIu64"\n", var->export_name->s, var->key, var->i);
	}
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE && left->vartype == ALLIGATOR_VARTYPE_COUNTER)
	{
		var->d = left->li;
		if (amtail_ll.vm > 1)
			fprintf(stderr, "load variable %s/%s: g/c %lf\n", var->export_name->s, var->key, var->d);
	}
	else if (var->type == ALLIGATOR_VARTYPE_COUNTER && left->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		var->i = left->ld;
		if (amtail_ll.vm > 1)
			fprintf(stderr, "load variable %s/%s: c/g %"PRIu64"\n", var->export_name->s, var->key, var->i);
	}
	else if (var->type == ALLIGATOR_VARTYPE_GAUGE && left->vartype == ALLIGATOR_VARTYPE_GAUGE)
	{
		var->d = left->ld;
		if (amtail_ll.vm > 1) {
			fprintf(stderr, "load variable %s/%s: g/g %lf, by %p(%"PRIu8")\n", var->export_name->s, var->key, var->d, var->by, var->by_count);
			if (var->by && var->by_count)
			{
				for (uint64_t i = 0; i < var->by_count; ++i)
					printf("\t\tby[%"PRIu64"] %s\n", i, var->by[i]->s);
			}
		}
	}
	else if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM)
	{
		if (left->vartype == ALLIGATOR_VARTYPE_COUNTER)
			amtail_histogram_observe(var, (double)left->li);
		else if (left->vartype == ALLIGATOR_VARTYPE_GAUGE)
			amtail_histogram_observe(var, left->ld);
	}
}
// TODO end

void amtail_vmfunc_inc(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!byte_ops || !byte_ops->export_name || !byte_ops->export_name->s)
		return;
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	if (var)
	{
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			++var->i;
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			++var->d;
	}
}

void amtail_vmfunc_dec(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (!byte_ops || !byte_ops->export_name || !byte_ops->export_name->s)
		return;
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l));
	if (var)
	{
		if (var->type == ALLIGATOR_VARTYPE_COUNTER)
			--var->i;
		else if (var->type == ALLIGATOR_VARTYPE_GAUGE)
			--var->d;
	}
}

void amtail_vmfunc_variable(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	uint32_t name_hash = amtail_hash(byte_ops->export_name->s, byte_ops->export_name->l);
	amtail_variable *var = alligator_ht_search(variables, amtail_variable_compare, byte_ops->export_name->s, name_hash);
	if (!var)
	{
		var = calloc(1, sizeof(*var));
		var->hidden = byte_ops->hidden;
		var->type = byte_ops->vartype;
		var->key = strdup(byte_ops->export_name->s);
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
		if (var->type == ALLIGATOR_VARTYPE_HISTOGRAM && !var->is_template)
			amtail_histogram_init(var);
		if (amtail_ll.vm > 1)
			printf("create variable %s with type %hhu and pointer: %p\n", byte_ops->export_name->s, byte_ops->vartype, var);
		alligator_ht_insert(variables, &(var->node), var, name_hash);
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
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_SETTIME] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_GETFILENAME] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_INT] = amtail_vmfunc_cast_int;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_BOOL] = amtail_vmfunc_cast_bool;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_FLOAT] = amtail_vmfunc_cast_float;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_STRING] = amtail_vmfunc_noop;
	amtail_vmfunc[AMTAIL_AST_OPCODE_FUNC_SUBST] = amtail_vmfunc_noop;
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

uint64_t amtail_branch_select(amtail_byteop *byte_ops, alligator_ht *variables, string *logline, uint64_t offset, uint64_t line_size, amtail_log_level amtail_ll)
{
	//if 
	//	(byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH)
	
		return amtail_vmfunc_branch(byte_ops, variables, logline, offset, line_size, amtail_ll);

	//return
}

int amtail_execute(amtail_thread *amt_thread, amtail_byteop *byte_ops, alligator_ht *variables, string *logline, amtail_log_level amtail_ll)
{
	if (byte_ops->opcode == AMTAIL_AST_OPCODE_BRANCH)
		return 2;
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

int amtail_run(amtail_bytecode* byte_code, string* logline, amtail_log_level amtail_ll)
{
	uint64_t size = byte_code->l;
	amtail_byteop *byte_ops = byte_code->ops;
	alligator_ht *variables = byte_code->variables;
	int rc;
	uint64_t line_size = 0;

	amtail_thread *amt_thread = amtail_thread_init();

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
		amtail_vm_stack_clear(amt_thread);
		for (uint64_t i = 0; i < size; ++i)
		{
			rc = amtail_execute(amt_thread, &byte_ops[i], variables, logline, amtail_ll);
			if (rc == 2) // branch
			{
				uint64_t new = amtail_branch_select(&byte_ops[i], variables, logline, cursym_log, line_size, amtail_ll);
				if (new)
					i = new;
			}
			else if (!rc)
			{
				printf("error execute on logline: '%s', %d\n", logline->s, rc);
				return rc;
			}
		}

		cursym_log += line_size;
		cursym_log += strspn(logline->s + cursym_log, "\n");
	}

	amtail_vm_stack_clear(amt_thread);
	amtail_thread_free(amt_thread);
	return 1;
}
