#include "amtail_pcre.h"

#define OVECCOUNT 240

regex_match* amtail_regex_compile(char *regexstring)
{
    regex_match *rematch = calloc(1, sizeof(*rematch));
    rematch->jstack = pcre_jit_stack_alloc(1000,10000);
    rematch->pattern = strdup(regexstring ? regexstring : "");

    int pcreErrorOffset;
    const char *pcreErrorStr;
    // 2 arg is option for pcre (PCRE_UTF8 and etc.)
    rematch->regex_compiled = pcre_compile(regexstring, 0, &pcreErrorStr, &pcreErrorOffset, NULL);

    if(rematch->regex_compiled == NULL) {
        printf("ERROR: Could not compile '%s': %s\n", regexstring, pcreErrorStr);
        if (rematch->jstack)
            pcre_jit_stack_free(rematch->jstack);
        free(rematch);
        return 0;
    }

    rematch->pcreExtra = pcre_study(rematch->regex_compiled, PCRE_STUDY_JIT_COMPILE, &pcreErrorStr);
    if(pcreErrorStr != NULL) {
        printf("ERROR: Could not study '%s': %s\n", regexstring, pcreErrorStr);
        if (rematch->jstack)
            pcre_jit_stack_free(rematch->jstack);
        pcre_free(rematch->regex_compiled);
        free(rematch);
        return 0;
    }

    return rematch;
}

void amtail_regex_free(regex_match *rematch)
{
    if (!rematch)
        return;

    if (rematch->pattern)
        free(rematch->pattern);
    if (rematch->pcreExtra)
        pcre_free_study(rematch->pcreExtra);
    if (rematch->regex_compiled)
        pcre_free(rematch->regex_compiled);
    if (rematch->jstack)
        pcre_jit_stack_free(rematch->jstack);
    free(rematch);
}

uint8_t amtail_regex_exec(regex_match *rematch, char *regex_match_string, uint64_t regex_match_size, amtail_log_level amtail_ll)
{
    if (!rematch)
    {
        if (amtail_ll.pcre > 0)
            fprintf(stderr, "amtail pcre error: null regex matcher\n");
        return 0;
    }

    int ovector[OVECCOUNT];

    //char temp_match[1255];
    //strlcpy(temp_match, regex_match_string, regex_match_size+1);
    //printf("\n\n'%s'\n", temp_match);

    int count = pcre_exec(rematch->regex_compiled, rematch->pcreExtra, regex_match_string, regex_match_size, 0, 0, ovector, OVECCOUNT);
    //printf("rc is %d\n", count);
    //printf("rc is %d, p is %p\n", count, tmp);
    if(count < 0)
    {
        if (count == PCRE_ERROR_NOMATCH)
        {
            ++rematch->nomatch;
            if (amtail_ll.pcre > 1)
            {
                int preview = regex_match_size < 160 ? (int)regex_match_size : 160;
                fprintf(stderr, "amtail pcre info: no match: /%s/ against '%.*s'%s\n", rematch->pattern ? rematch->pattern : "<unknown>", preview, regex_match_string ? regex_match_string : "", regex_match_size > (uint64_t)preview ? "..." : "");
            }
            return 0;
        }

        switch(count)
        {
            case PCRE_ERROR_NULL:
                ++rematch->null;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: PCRE_ERROR_NULL\n");
                break;
            case PCRE_ERROR_BADOPTION:
                ++rematch->badoption;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: PCRE_ERROR_BADOPTION\n");
                break;
            case PCRE_ERROR_BADMAGIC:
                ++rematch->badmagic;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: PCRE_ERROR_BADMAGIC\n");
                break;
            case PCRE_ERROR_UNKNOWN_NODE:
                ++rematch->unknown_node;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: PCRE_ERROR_UNKNOWN_NODE\n");
                break;
            case PCRE_ERROR_NOMEMORY:
                ++rematch->nomemory;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: PCRE_ERROR_NOMEMORY\n");
                break;
            default:
                ++rematch->unknown_error;
                if (amtail_ll.pcre > 0)
                    fprintf(stderr, "amtail pcre error: unknown pcre_exec code %d\n", count);
                break;
        }
        return 0;
    }

    if (amtail_ll.pcre > 1)
        fprintf(stderr, "amtail pcre info: matched /%s/, groups=%d\n", rematch->pattern ? rematch->pattern : "<unknown>", count - 1);

    if (amtail_ll.pcre > 2)
    {
        uint8_t i;
        for (i = 1; i < count; i++) {
            char *substring_start = regex_match_string + ovector[2*i];
            int substring_length = ovector[2*i+1] - ovector[2*i];
            fprintf(stderr, "%2d: %.*s\n", i, substring_length, substring_start);
        }
    }

    return count;
}
