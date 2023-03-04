#include "amtail_pcre.h"

#define OVECCOUNT 240

regex_match* amtail_regex_compile(char *regexstring)
{
    regex_match *rematch = calloc(1, sizeof(*rematch));
    rematch->jstack = pcre_jit_stack_alloc(1000,10000);

    int pcreErrorOffset;
    const char *pcreErrorStr;
    // 2 arg is option for pcre (PCRE_UTF8 and etc.)
    rematch->regex_compiled = pcre_compile(regexstring, 0, &pcreErrorStr, &pcreErrorOffset, NULL);

    if(rematch->regex_compiled == NULL) {
        printf("ERROR: Could not compile '%s': %s\n", regexstring, pcreErrorStr);
        free(rematch);
        pcre_jit_stack_free(rematch->jstack);
        return 0;
    }

    rematch->pcreExtra = pcre_study(rematch->regex_compiled, PCRE_STUDY_JIT_COMPILE, &pcreErrorStr);
    if(pcreErrorStr != NULL) {
        printf("ERROR: Could not study '%s': %s\n", regexstring, pcreErrorStr);
        free(rematch);
        pcre_jit_stack_free(rematch->jstack);
        pcre_free(rematch->regex_compiled);
        return 0;
    }

    return rematch;
}

uint8_t amtail_regex_exec(regex_match *rematch, char *regex_match_string, uint64_t regex_match_size)
{
    if (!rematch)
        return 0;

    int ovector[OVECCOUNT];

    //char temp_match[1255];
    //strlcpy(temp_match, regex_match_string, regex_match_size+1);
    //printf("\n\n'%s'\n", temp_match);

    int count = pcre_exec(rematch->regex_compiled, rematch->pcreExtra, regex_match_string, regex_match_size, 0, 0, ovector, OVECCOUNT);
    //printf("rc is %d\n", count);
    //printf("rc is %d, p is %p\n", count, tmp);
    if(count < 0)
    {
        switch(count)
        {
            case PCRE_ERROR_NOMATCH: ++rematch->nomatch ;break;
            case PCRE_ERROR_NULL: ++rematch->null; break;
            case PCRE_ERROR_BADOPTION: ++rematch->badoption; break;
            case PCRE_ERROR_BADMAGIC: ++rematch->badmagic; break;
            case PCRE_ERROR_UNKNOWN_NODE: ++rematch->unknown_node; break;
            case PCRE_ERROR_NOMEMORY: ++rematch->nomemory; break;
            default: ++rematch->unknown_error; break;
        }
        return 0;
    }

    //printf("\nOK, has matched ...\n\n");

    uint8_t i;
    for (i = 1; i < count; i++) {
            char *substring_start = regex_match_string + ovector[2*i];
            int substring_length = ovector[2*i+1] - ovector[2*i];
            printf("%2d: %.*s\n", i, substring_length, substring_start);
    }

    return count;
}
