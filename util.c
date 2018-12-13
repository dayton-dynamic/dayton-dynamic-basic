/*
    Dayton Dynamic BASIC
    baby utilities
    MWA 2018
*/

#include "all.h"

int Mallocs, Frees;     /* diagnostic memory counts */
lego *legos;            /* free legos */
char *warning;          /* first encountered with most recent line typed */
int noANSI;             /* do not output escape sequences */
int ctrl_c;             /* provision to stop running program */
FILE *urandom;          /* CSPRNG source */

static char *prompt = "Ok\n";

/*
 *  Get zeroed memory or perish.
 */
void *getmem(int bytes)
{
    void *m = calloc(bytes, 1);

    if (!m) {
        puts("out of memory");
        exit(1);
    }

    ++Mallocs;
    return m;
}

/*
 *  Allocate and copy a (sub)string.
 *  If 'upto' is NULL, the whole string (to \0) will be copied.
 */
char *copySubstring(char *from, char *upto)
{
    int len = upto ? upto - from : strlen(from);
    char *res = getmem(1 + len);

    memcpy(res, from, len);
    res[len] = '\0';
    return res;
}

/*
 *  Get a lego from the free pile or the "box."
 *
 *  This approach probably adds more complexity than value for this use.
 *  Straight malloc and free would be okay, especially considering all
 *  the string processing and other places we go that route, and also
 *  considering that most malloc and free use would be for long-running
 *  BASIC code, not the up-front parsing of the program.
 */
lego *newLego(int what)
{
    lego *l;

    if (legos)
        l = legos, legos = l -> next;
    else
        l = getmem(sizeof(lego));

    memset(l, 0, sizeof(lego));
    l -> what = what;
    return l;
}

/*
 *  Return a lego to the free pile.
 */
void byeLego(lego *tree)
{
    lego *next;
    int i;

    for (; tree; tree = next) {
        for (i=0; i<max_args; i++)
            byeLego(tree -> a[i]);
        if (tree -> s)
            zap(tree -> s);
        next = tree -> next;
        tree -> next = legos;
        legos = tree;
    }
}

/*
 *  I only provide for one error message per line entered, because
 *  further messages usually are caused by cascading failures that
 *  will only confuse the programmer.
 */
void warn(char *why)
{
    if (!warning)
        warning = why;
}

/*
 *  Catch CTRL-C as a break signal for DDB.
 */
static void see_ctrl_c(int ignored)
{
    ctrl_c = 1;
}

/*
 *  Read input line of arbitrary size. Does not copy \n.
 */
char *read_line(void)
{
    static char *buf = NULL;
    static int len = 4;
    int offset = 0;
    char *s, *t;

    if (!buf) {
        buf = getmem(len);
        --Mallocs;                  /* just 1, can't free easily, so no count */
    }

    while (1) {
        s = fgets(buf + offset, len - offset, stdin);
        if (!s) {
            if (ctrl_c) {           /* no big deal - let user retype */
                printf("\n");
                ctrl_c = 0, offset = 0;
                continue;
            }
            return NULL;            /* end of file from user */
        }

        t = strchr(s, '\n');
        if (t) {                    /* line is complete */
            *t = '\0';
            return buf;
        }

        /* buffer was not large enough */
        offset = strlen(buf);
        buf = realloc(buf, len *= 2);
        if (!buf) {
            puts("out of memory");
            exit(1);
        }
    }
}

/*
 *  A simple read-eval-print loop in Dayton Dynamic BASIC.
 *  I don't have the ability to store or run a program yet,
 *  so I hope you'll come to my next talk on December 14, 2018.
 */
int main(int argc, char **argv)
{
    char *s;
    lego *l;
    int ok;
    struct sigaction act = { 0 };

    forceParens = !!getenv("PARENS");
    noANSI = !!getenv("NOANSI");
    urandom = fopen("/dev/urandom", "rb");
    act.sa_handler = see_ctrl_c;
    if (sigaction(SIGINT, &act, NULL))
        perror("issue with sigaction");

    flash('h'); printf("%s", prompt); flash('n');
    while (1) {
        warning = NULL;             /* TODO: refactor error output */
        s = read_line();
        if (!s)
            break;
        ok = command_line(&s, &l);
        if (warning) {
            flash('e'); puts(warning); flash('n');
            warning = NULL;
        }
        if (ok) {
            if (immediate(l)) {     /* returns true iff "free to free" */
                byeLego(l);
                flash('h'); printf("%s", prompt); flash('n');
            }
        }
    }

    /* Clean up the free pile AFTER deleting the program. */
    erase_program();
    for (; legos; legos = l) {
        l = legos -> next;
        zap(legos);
    }

    if (Mallocs != Frees)
        printf("%i mallocs and %i frees.\n", Mallocs, Frees);
    return 0;
}

/*
 *  I hope you're using an ANSI terminal; you'll get color output and
 *  be able to clear your display (the CLS commmand). If that isn't
 *  working, you can set a NOANSI environment variable.
 */
void flash(int style)
{
    char *esc;
    if (noANSI) return;
    switch (style) {
        case 'c': esc = "\x1b[H\x1b[2J\x1b[3J"; break;
        case 'h': esc = "\x1b[1;32m"; break;
        case 'e': esc = "\x1b[1;31m"; break;
        case 'n': default: esc = "\x1b[m"; break;
    }
    printf("%s", esc);
    fflush(stdout);
}
