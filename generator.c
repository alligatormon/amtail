#include "common/selector.h"
#include "parser.h"
#include "generator.h"
#include <string.h>
#include "amtail_pcre.h"

#define AMTAIL_MAX_LABELS 255

static int amtail_ast_node_valid(amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (!ast)
		return 0;

	if (ast->opcode > AMTAIL_AST_OPCODE_RUN)
	{
		if (amtail_ll.generator > 0)
			printf("skip invalid opcode: %u\n", ast->opcode);
		return 0;
	}

	if (ast->by_count > AMTAIL_MAX_LABELS || ast->bucket_count > AMTAIL_MAX_LABELS)
	{
		if (amtail_ll.generator > 0)
			printf("skip invalid label counts: by=%u bucket=%u\n", ast->by_count, ast->bucket_count);
		return 0;
	}

	if (ast->name && (!ast->name->s || ast->name->l > (1u << 20)))
	{
		if (amtail_ll.generator > 0)
			printf("skip invalid ast->name\n");
		return 0;
	}

	return 1;
}

static void copy_labels(string **src, uint8_t src_count, string ***dst, uint8_t *dst_count)
{
	if (!src || !src_count || !dst || !dst_count)
		return;

	*dst = calloc(1, sizeof(**dst) * src_count);
	*dst_count = src_count;
	for (uint8_t i = 0; i < src_count; ++i)
	{
		if (src[i] && src[i]->s && src[i]->l)
			(*dst)[i] = string_init_alloc(src[i]->s, src[i]->l);
	}
}

static void compile_regex_for_op(amtail_byteop *op, amtail_log_level amtail_ll)
{
	if (!op || !op->export_name || !op->export_name->s || !op->export_name->l)
		return;

	if (op->opcode != AMTAIL_AST_OPCODE_BRANCH &&
	    op->opcode != AMTAIL_AST_OPCODE_MATCH &&
	    op->opcode != AMTAIL_AST_OPCODE_NOTMATCH)
		return;

	char *pattern = NULL;
	size_t pattern_len = 0;

	if (op->opcode == AMTAIL_AST_OPCODE_MATCH || op->opcode == AMTAIL_AST_OPCODE_NOTMATCH)
	{
		char *first = strchr(op->export_name->s, '/');
		char *last = strrchr(op->export_name->s, '/');
		if (!first || !last || first == last)
			return;

		pattern = first + 1;
		pattern_len = (size_t)(last - first - 1);
	}
	else
	{
		pattern = op->export_name->s;
		pattern_len = op->export_name->l;
	}

	/*
	 * Parser uses BRANCH for pure /.../ blocks and for condition forms
	 * like "$message /.../". The latter are not standalone regex patterns.
	 */
	if (op->opcode == AMTAIL_AST_OPCODE_BRANCH &&
	    (pattern[0] == '$' || strstr(pattern, " /") || strstr(pattern, " $")))
		return;

	/* Keep parser output intact; trim only canonical /.../ wrapper if present. */
	if (op->opcode == AMTAIL_AST_OPCODE_BRANCH &&
	    pattern_len >= 2 && pattern[0] == '/' && pattern[pattern_len - 1] == '/')
	{
		++pattern;
		pattern_len -= 2;
	}

	char *compiled_pattern = strndup(pattern, pattern_len);
	if (!compiled_pattern)
		return;

	op->re_match = amtail_regex_compile(compiled_pattern);
	if (amtail_ll.generator > 0)
		printf("compile regexp for opcode %u: %p: '%s'\n", op->opcode, op->re_match, compiled_pattern);
	free(compiled_pattern);
}

typedef struct ast_walk_ctx {
	amtail_ast **seen;
	uint64_t seen_count;
	uint64_t seen_cap;
} ast_walk_ctx;

static int ast_walk_seen(ast_walk_ctx *ctx, amtail_ast *node)
{
	if (!ctx || !node)
		return 0;

	for (uint64_t i = 0; i < ctx->seen_count; ++i)
	{
		if (ctx->seen[i] == node)
			return 1;
	}
	return 0;
}

static int ast_walk_mark(ast_walk_ctx *ctx, amtail_ast *node)
{
	if (!ctx || !node)
		return 0;
	if (ast_walk_seen(ctx, node))
		return 1;

	if (ctx->seen_count >= ctx->seen_cap)
	{
		uint64_t new_cap = ctx->seen_cap ? ctx->seen_cap * 2 : 256;
		amtail_ast **new_seen = realloc(ctx->seen, sizeof(*new_seen) * new_cap);
		if (!new_seen)
			return 0;
		ctx->seen = new_seen;
		ctx->seen_cap = new_cap;
	}

	ctx->seen[ctx->seen_count++] = node;
	return 1;
}

amtail_bytecode* amtail_code_init(uint64_t size)
{
	amtail_bytecode *byte_code = calloc(1, sizeof(*byte_code));
	byte_code->ops = calloc(1, sizeof(amtail_byteop) * size);

	byte_code->m = size;
	byte_code->l = 0;

	return byte_code;
}

void amtail_byteops_free(amtail_byteop *arg)
{
	free(arg);
}

void amtail_code_push(amtail_bytecode *byte_code, amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (!amtail_ast_node_valid(ast, amtail_ll))
		return;

	if (byte_code->l >= byte_code->m)
	{
		byte_code->m *= 2;
		amtail_byteop *new = calloc(1, sizeof(amtail_byteop) * byte_code->m);
		memcpy(new, byte_code->ops, byte_code->l * sizeof(amtail_byteop));
		amtail_byteops_free(byte_code->ops);
		byte_code->ops = new;
	}

	amtail_byteop *fill = &byte_code->ops[byte_code->l];
	fill->opcode = ast->opcode;
	fill->vartype = ast->vartype;
	fill->facttype = ast->facttype;
	fill->hidden = ast->hidden;
	if (fill->opcode == AMTAIL_AST_OPCODE_VARIABLE)
	{
		copy_labels(ast->by, ast->by_count, &fill->by, &fill->by_count);
		copy_labels(ast->bucket, ast->bucket_count, &fill->bucket, &fill->bucket_count);
	}

	if (ast->vartype == ALLIGATOR_VARTYPE_CONST && ast->name && ast->name->s)
	{
		if (ast->facttype == ALLIGATOR_FACTTYPE_INT)
			fill->li = ast->ivalue;
		else if (ast->facttype == ALLIGATOR_FACTTYPE_DOUBLE)
			fill->ld = ast->dvalue;
		else if (ast->facttype == ALLIGATOR_FACTTYPE_TEXT)
			fill->ls = string_string_init_dup(ast->svalue);
	}

	if (ast->export_name)
	{
		fill->export_name = string_new();
		string_string_copy(fill->export_name, ast->export_name);
        if (amtail_ll.generator > 0)
		    printf("> byteop %p exort_name %s\n", fill, fill->export_name->s);
	}

	if (ast->name && ast->name->s && ast->name->l && !ast->export_name)
	{
		if (amtail_ll.generator > 0)
			printf("fill %p name %s\n", ast, ast->name->s);
		fill->export_name = string_new();
		string_string_copy(fill->export_name, ast->name);
		if (amtail_ll.generator > 0)
			printf("> byteop %p name %s\n", fill, fill->export_name->s);
	}

	compile_regex_for_op(fill, amtail_ll);

	if (fill->opcode == AMTAIL_AST_OPCODE_VAR)
	{
		if (ast->vartype == ALLIGATOR_VARTYPE_COUNTER)
		{
			fill->li = ast->ivalue;
			if (amtail_ll.generator > 0 && fill->export_name)
				printf("assign integer: %p: '%s' -> %"PRId64"\n", fill, fill->export_name->s, fill->li);
		}
		else if (ast->vartype == ALLIGATOR_VARTYPE_GAUGE)
		{
			fill->ld = ast->dvalue;
			if (amtail_ll.generator > 0 && fill->export_name)
				printf("assign double: %p: '%s' -> %lf\n", fill, fill->export_name->s, fill->ld);
		}
	}

	++byte_code->l;
}

#define AMTAIL_MAX_AST_WALK_DEPTH 512
typedef struct walk_frame {
	amtail_ast *node;
	uint64_t index;
	uint32_t depth;
	uint8_t stage; /* 0: enter, 1: after-left, 2: done */
} walk_frame;

static void walk_stack_push(walk_frame **stack, uint64_t *len, uint64_t *cap, walk_frame frame)
{
	if (*len >= *cap)
	{
		uint64_t new_cap = *cap ? (*cap * 2) : 256;
		walk_frame *new_stack = realloc(*stack, sizeof(*new_stack) * new_cap);
		if (!new_stack)
			return;
		*stack = new_stack;
		*cap = new_cap;
	}
	(*stack)[(*len)++] = frame;
}

static void amtail_bytecode_walk_iterative(amtail_bytecode *byte_code, amtail_ast *root, amtail_log_level amtail_ll, ast_walk_ctx *ctx)
{
	if (!byte_code || !root)
		return;

	walk_frame *stack = NULL;
	uint64_t stack_len = 0;
	uint64_t stack_cap = 0;
	walk_stack_push(&stack, &stack_len, &stack_cap, (walk_frame){ .node = root, .index = 0, .depth = 0, .stage = 0 });

	while (stack_len)
	{
		walk_frame *f = &stack[stack_len - 1];
		amtail_ast *node = f->node;
		if (!node)
		{
			--stack_len;
			continue;
		}

		if (f->stage == 0)
		{
			if (f->depth > AMTAIL_MAX_AST_WALK_DEPTH)
			{
				if (amtail_ll.generator > 0)
					printf("warning: ast walk depth exceeded (%u), skip node %p\n", f->depth, node);
				--stack_len;
				continue;
			}

			if (ctx && ast_walk_seen(ctx, node))
			{
				if (amtail_ll.generator > 1)
					printf("debug: skip already-seen node %p\n", node);
				--stack_len;
				continue;
			}

			if (ctx && !ast_walk_mark(ctx, node))
			{
				if (amtail_ll.generator > 0)
					printf("warning: cannot mark node %p, skipping subtree\n", node);
				--stack_len;
				continue;
			}

			f->index = byte_code->l;
			amtail_code_push(byte_code, node, amtail_ll);
			f->stage = 1;

			if (node->stem && node->stem[AMTAIL_AST_LEFT] && node->stem[AMTAIL_AST_LEFT] != node)
			{
				walk_stack_push(&stack, &stack_len, &stack_cap,
				                (walk_frame){ .node = node->stem[AMTAIL_AST_LEFT], .index = 0, .depth = f->depth + 1, .stage = 0 });
			}
			continue;
		}

		if (f->stage == 1)
		{
			f->stage = 2;
			if (node->stem && node->stem[AMTAIL_AST_RIGHT] && node->stem[AMTAIL_AST_RIGHT] != node)
			{
				if (f->index < byte_code->l)
					byte_code->ops[f->index].right_opcounter = byte_code->l ? byte_code->l - 1 : 0;

				walk_stack_push(&stack, &stack_len, &stack_cap,
				                (walk_frame){ .node = node->stem[AMTAIL_AST_RIGHT], .index = 0, .depth = f->depth + 1, .stage = 0 });
			}
			continue;
		}

		--stack_len;
	}

	free(stack);
}

amtail_bytecode* amtail_code_generator(amtail_ast *ast, amtail_log_level amtail_ll)
{
	if (!ast)
		return NULL;

	amtail_bytecode *byte_code = amtail_code_init(1);
	ast_walk_ctx walk_ctx = {0};

	amtail_bytecode_walk_iterative(byte_code, ast, amtail_ll, &walk_ctx);
	free(walk_ctx.seen);

	return byte_code;
}

void amtail_code_free(amtail_bytecode *byte_code)
{
	if (!byte_code)
		return;

	for (uint64_t i = 0; i < byte_code->l; ++i)
	{
		amtail_byteop *ops = &byte_code->ops[i];
		if (ops->export_name)
			string_free(ops->export_name);

		if (ops->by_count)
		{
			for (uint64_t j = 0; j < ops->by_count; ++j)
				if (ops->by[j])
					string_free(ops->by[j]);

			free(ops->by);
		}

		if (ops->bucket_count)
		{
			for (uint64_t j = 0; j < ops->bucket_count; ++j)
				if (ops->bucket[j])
					string_free(ops->bucket[j]);

			free(ops->bucket);
		}

		if (ops->vartype == ALLIGATOR_VARTYPE_CONST &&
		    ops->facttype == ALLIGATOR_FACTTYPE_TEXT &&
		    ops->ls)
			string_free(ops->ls);

		if (ops->re_match)
			amtail_regex_free(ops->re_match);
	}


	free(byte_code->ops);
	free(byte_code);
}
