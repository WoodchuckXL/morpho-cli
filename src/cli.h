/** @file cli.h
 *  @author T J Atherton
 *
 *  @brief Command line interface
*/

#ifndef cli_h
#define cli_h

#include <morpho.h>
#include <varray.h>
#include <common.h>

#include "inline.h"
#include "hlp.h"

#include "debugger.h"

#define CLI_DEFAULTCOLOR -1
#define CLI_ERRORCOLOR  INLINE_RED
#define CLI_WARNINGCOLOR  INLINE_YELLOW

#define CLI_NOEMPHASIS  -1
#define CLI_BOLD        0
#define CLI_UNDERLINE   1
#define CLI_ITALIC      2

#define CLI_PROMPT "> "
#define CLI_CONTINUATIONPROMPT "~ "
#define CLI_QUIT "quit"
#define CLI_HELP "help"
#define CLI_SHORT_HELP "?"

#define CLI_RUN                 (1<<0)
#define CLI_DISASSEMBLE         (1<<1)
#define CLI_DISASSEMBLESHOWSRC  (1<<2)
#define CLI_DEBUG               (1<<3)
#define CLI_OPTIMIZE            (1<<4)
#define CLI_PROFILE             (1<<5)
#define CLI_INTERACTIVE         (1<<6)
#define CLI_NOCOLOR             (1<<7)

typedef unsigned int clioptions;

extern char *cli_globalsrc;

void cli_displaywithstyle(int col, int emph, int n, ...);
void cli_reporterror(error *err, vm *v);

void cli_run(const char *in, clioptions opt);
void cli_runstring(const char *src, clioptions opt);
void cli(clioptions opt);

char *cli_loadsource(const char *in);
char *cli_loadstdin(void);
void cli_disassemblewithsrc(program *p, char *src, clioptions opt);
void cli_list(const char *in, int start, int end, clioptions opt);

#endif /* cli_h */
