/*
    Dayton Dynamic BASIC
    includes
    MWA 2018

    NON-OBVIOUS MISSING FEATURES
    ----------------------------
    string comparison
    arrays
    variable linkage
    ON ERROR
    file I/O
    load and save
    low-power sleep
    time and date
    INKEY$
    line renumbering
    line editing
*/

#include <ctype.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <signal.h>

/*
 *  The BASIC parser takes input lines and outputs a tree of
 *  "building blocks" that I have named legos. Hopefully I won't
 *  get in trouble for infringing any trademarks. Legos are very
 *  general and sort of one-size-does-everything, so they have
 *  a lot of fields. Most of these fields are usually empty,
 *  somewhat the same as toy Legos often having "bumps" that
 *  aren't covered by other Legos.
 */
enum { max_args = 4 };

typedef struct lego {
    int what;           // w-enum
    double n;           // number (if any)
    char *s;            // string (if any)
    void *link;         // linked pointer (to line or var)
    char force_parens;  // print parens around this lego
    char lit_delim;     // alternate delimiter for string literal
    char list_delim;    // comma (0) or semicolon (1) for PRINT
    char abbrev;        // abbreviate PRINT or REM to ? or '
    struct lego 
        *a[max_args];   // sub-lego arguments, parameters, etc.
    struct lego *next;  // for lists of expressions, line #s, etc.
} lego;

/*
 *  This is how a computed result from an expression is returned.
 *  Whoever owns this typedef needs to free 's' when done.
 */
typedef struct {
    enum { rNum = -1, rExcept = 0, rString = 1 } what;
    double n;
    char *s;
} computed;

/*
 *  Here are supported 'what' values for legos, along with corresponding
 *  string representations.
 */
enum {
    wZERO_IS_UNUSED, wNEGATE, wNOT, END_UNARY_GUYS,

    wCAT, wPOWER, wMUL, wDIV, wADD, wSUB, wIDIV, wMOD,
    wGT, wGE, wLT, wLE, wEQ, wNE,  
    wAND, wOR, wXOR, wEQV, wIMP, wNAND, wNOR, END_BINARY_GUYS,

    wABS, wASC, wATAN, wCHR, wCOS, wEXP, wFIX, wINSTR, wINT,
    wLEFT, wLEN, wLOG, wMID, wRIGHT, wRND, wSGN, wSIN, wSPACE,
    wSQRT, wSTR, wSTRING, wTAN, wVAL, END_FUNCTION_GUYS,

    wNEW, wEND, wSTOP, wCONT, wRETURN, wCLS, wLIST, wDEL, wGOSUB, wGOTO, 
    wRUN, wRESTORE, wONGOTO, wONGOSUB, wREM, wFOR, wNEXT, wREAD, wDATA, 
    wPRINT, wINPUT, wIF, wLET, wLINEINPUT, wALTER, wONALTER, END_STATEMENT_GUYS,

    wKLUDGE, wSTRLIT, wSTRVAR, wNUMLIT, wNUMVAR, wLINENUM,
    wNUMBEREDLINE, wERROR,

#define GUYS \
    "o.unused", "-", "NOT ", "e.un", \
    "+", "^", "*", "/", "+", "-", \
    "\\", "MOD", ">", ">=", "<", "<=", "=", "<>", \
    "AND", "OR", "XOR", "EQV", "IMP", "NAND", "NOR", "e.bin", \
    "ABS", "ASC", "ATAN", "CHR$", "COS", "EXP", "FIX", "INSTR", "INT", \
    "LEFT$", "LEN", "LOG", "MID$", "RIGHT$", "RND", "SGN", "SIN", "SPACE$", \
    "SQRT", "STR$", "STRING$", "TAN", "VAL", "e.fun", \
    "NEW", "END", "STOP", "CONT", "RETURN", "CLS", "LIST", "DEL", "GOSUB", \
    "GOTO", "RUN", "RESTORE", "ONGOTO", "ONGOSUB", "REM", "FOR", "NEXT", \
    "READ", "DATA", "PRINT", "INPUT", "IF", "LET", "LINEINPUT", "ALTER", \
    "ONALTER", "e.st", "o.kludge", "o.strlit", "o.strvar", "o.numlit", \
    "o.numvar", "o.linenum", "o.numberedline", "o.error"
};

/* eval.c */
void erase_run_vars(void);
computed evalloc(lego *l);
void set_var(char *name, char *s, double n);
double num_from_name_hack(char *name);

/* parser.c */
int nothingMore(char **s);
int command_line(char **ss, lego **result);
int str_lit(char **ss, lego **result);
int unquoted_str_lit(char **ss, lego **result);
int num_lit(char **ss, lego **result);
int symbol(char **ss, char *sym);

/* print.c */
extern int forceParens;
extern char *guys[];
void printLego(lego *l);

/* run.c */
int immediate(lego *l);
void erase_program(void);

/* util.c */
extern int Mallocs, Frees;
extern int ctrl_c;
extern FILE *urandom;
extern char *warning;
char *copySubstring(char *from, char *upto);
lego *newLego(int what);
void byeLego(lego *tree);
int main(int argc, char **argv);
void warn(char *why);
void flash(int style);
void *getmem(int bytes);
char *read_line(void);

#define ifnot(x) if (!(x))
#define zap(x) { if (x) { free(x); ++Frees; (x) = NULL; } }
#define tr(x) { fprintf(stderr, "%s", #x); }
