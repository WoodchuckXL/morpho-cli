/** @file main.c
 *  @author T J Atherton
 *
 *  @brief Main entry point and process options
 */

#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

#include <morpho.h>

#include "cli.h"
#include "debugger.h"

/** Option handler
 * @param[in] opt - option to process
 * @param[out] flags - flags to modify
 * @returns: control program execution: true to execute program, false if not */
typedef bool (*optionfn)(const char *opt, clioptions *flags);

static bool opt_version(const char *opt, clioptions *flags) { // Display version
    version v;
    morpho_version(&v);
    char buf[VERSION_MAXSTRINGLENGTH];
    version_tostring(&v, VERSION_MAXSTRINGLENGTH, buf);
    printf("Morpho v%s\n", buf);
    return false;
}

static bool opt_disassembleonly(const char *opt, clioptions *flags) { // Disassemble only
    *flags ^= CLI_RUN;
    *flags |= CLI_DISASSEMBLE;
    return false;
}

static bool opt_disassemblelist(const char *opt, clioptions *flags) { // Disassemble & list
    *flags |= CLI_DISASSEMBLE | CLI_DISASSEMBLESHOWSRC;
    return true;
}

static bool opt_disassemble(const char *opt, clioptions *flags) { // Disassemble before running
    *flags |= CLI_DISASSEMBLE;
    return true;
}

static bool opt_debug(const char *opt, clioptions *flags) { // Enable debugging
    *flags |= CLI_DEBUG;
    return true;
}

static bool opt_optimize(const char *opt, clioptions *flags) { // Enable optimization
    (void)opt;
    *flags |= CLI_OPTIMIZE;
    return true;
}

static bool opt_profile(const char *opt, clioptions *flags) { // Enable profiling
    (void)opt;
#ifdef MORPHO_PROFILER
    *flags |= CLI_PROFILE;
#endif
    return true;
}

static bool opt_workers(const char *opt, clioptions *flags) { // Set number of worker threads
    (void)flags;
    const char *c = opt + 1;
    while (*c && *c != '=' && !isdigit((unsigned char)*c)) c++;
    if (*c == '=') c++;
    int n = isdigit((unsigned char)*c) ? atoi(c) : 0;
    if (n < 0) n = 0;
    morpho_setthreadnumber(n);
    return true;
}

typedef struct {
    const char *s, *l;
    optionfn fn;
} option_t;

static const option_t opt_table[] = {
    { "-D",       NULL,            opt_disassembleonly },
    { "-dl",      NULL,            opt_disassemblelist },
    { "-d",       "--disassemble", opt_disassemble },
    { "-debug",   "--debug",       opt_debug },
    { "-O",       "--optimize",    opt_optimize },
#ifdef MORPHO_PROFILER
    { "-profile", "--profile",     opt_profile },
#endif
    { "-v",       "--version",     opt_version },
    { "-w",       "--workers",     opt_workers },
    { NULL,       NULL,            NULL },
};

static bool parse_option(const char *arg, clioptions *flags) {
    for (int j = 0; opt_table[j].s || opt_table[j].l; j++) {
        const char *s = opt_table[j].s, *l = opt_table[j].l;
        if ((s && strncmp(arg, s, strlen(s)) == 0) ||
            (l && strncmp(arg, l, strlen(l)) == 0))
            return opt_table[j].fn(arg, flags);
    }
    printf("Unknown option: '%s'\n", arg);
    return false;
}

int main(int argc, const char *argv[]) {
    clioptions opt = CLI_RUN;
    const char *file = NULL;
    int i = 1;
    bool run = true;

    morpho_initialize();

    for (; i < argc; i++) { // Process args
        const char *arg = argv[i];
        if (arg[0] == '-') {
            run &= parse_option(arg, &opt);
        } else {
            file = arg;
            break;
        }
    }

    if (run) {
        clidebugger_initialize();
        if (i < argc) morpho_setargs(argc - i - 1, argv + i);
        (file ? cli_run(file, opt) : cli(opt));
    }

    morpho_finalize();
    return 0;
}
