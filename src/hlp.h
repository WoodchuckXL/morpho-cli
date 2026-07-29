/** @file hlp.h
 *  @author T J Atherton
 *
 *  @brief Interactive help system
*/

#ifndef hlp_h
#define hlp_h

#include <stdio.h>

#include <morpho.h>
#include <object.h>

#include "inline.h"

#ifndef MORPHO_INCLUDE_HELP

extern objecttype objecthelptopictype;
#define OBJECT_HELPTOPIC objecthelptopictype

typedef struct sobjecthelptopic {
    object obj;
    char *topic; // Topic name
    char *file; // File
    long int location; // Location in file
    struct sobjecthelptopic *parent; // Parent topic
    struct sobjecthelptopic *next; // Next topic (global linked list)
    dictionary subtopics; // Subtopics
} objecthelptopic;

#define HELP_INDEXPAGE "help"
#define HELP_TOPICS "Topics:\n"
#define HELP_SUBTOPICS "Subtopics:\n"

size_t help_querylength(char *query, char **s);
objecthelptopic *help_search(char *query);
void help_display(inline_editor *edit, objecthelptopic *topic);

#else
#include <help.h>

#define HLP_TOPICS_HDR "Topics:\n"
#define HLP_SUBTOPICS_HDR "Subtopics:\n"

void hlp_displaytopic(inline_editor *edit, const help_topic *t);

void hlp_displaytopiclist(inline_editor *edit, varray_value *topics, const char *heading);

#endif

bool hlp_initialize(void);
void hlp_finalize(void);

#endif /* hlp_h */
