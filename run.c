/*
    Dayton Dynamic BASIC
    interpreter
    MWA 2018
*/

#include "all.h"

/*
 *  Stack that enables NEXT to work.
 */
typedef struct next_stack {
    char *nVar;         /* name of numeric variable */
    double vi;          /* initial value */
    double de;          /* ending value (if in right direction) */
    double step;        /* increment */
    lego *line;         /* line to return to at NEXT */
    lego *stmt;         /* statement in line to return to at NEXT */
    struct next_stack *next;
} next_stack;

/*
 *  Stack that enables RETURN to work.
 */
typedef struct ret_stack {
    lego *line;         /* NUMBERED line to return to, if any */
    lego *stmt;         /* statement within line to return to, if any */
    int running;        /* whether returning to program or immediate */
    struct ret_stack *next;
} ret_stack;

/*
 *  Execution context. We'll need one for the BASIC program
 *  (with line numbers), and one for immediate commands (no line numbers)
 *  that could happen between STOP and CONT.
 */
typedef struct x_con {
    lego *line;             /* current NUMBERED line within program, if any */
    lego *stmt;             /* statement within line to execute */
    lego *data_line;        /* RESTORE points here */
    lego *data_stmt;        /* READ advances this */
    lego *data_datum;       /* READ also advances this */
    ret_stack *ret_to;      /* where to go when we see a RETURN */
    next_stack *next_to;    /* where to go when we see a NEXT */
    double lNum;            /* number of current line, or < 0 for immediate */
} x_con;

static lego *program;   /* sorted, linked list of line #s w/ attached code */
static int dirty;       /* "dirty" means not in column 1 of output */
static x_con prog_con;  /* context of current program */
static lego *start_at;  /* position to start at (e.g. "RUN 500") */

/*
 *  Prevent program from continuing, but keep it and its vars.
 */
void reset_program(void)
{
    ret_stack *rs;
    next_stack *ns;

    /* Kill all GOSUB subroutines in progress. */
    while (prog_con.ret_to) {
        rs = prog_con.ret_to -> next;
        zap(prog_con.ret_to);
        prog_con.ret_to = rs;
    }

    /* Kill all FOR loops in progress. */
    while (prog_con.next_to) {
        ns = prog_con.next_to -> next;
        zap(prog_con.next_to -> nVar);
        zap(prog_con.next_to);
        prog_con.next_to = ns;
    }

    /* Reinitialize the program context. */
    memset(&prog_con, 0, sizeof(prog_con));
    prog_con.data_line = program;
}

/*
 *  Remove program.
 */
void erase_program(void)
{
    lego *bye;

    reset_program();

    /* This loop removes a line at a time from the program. */
    while (program) {
        bye = program -> next;
        program -> next = NULL;
        byeLego(program);
        program = bye;
    }

    erase_run_vars();
}

/*
 *  Find a numbered line within 'program'.
 *  Note ALL siblings of 'program' are wNUMBEREDLINE, but nothing else is.
 *  References to wNUMBEREDLINE legos are wLINENUM legos.
 */
lego *find_line(double lNum)
{
    lego *l;
    for (l = program; l; l = l -> next)
        if (l -> n == lNum) return l;
    return NULL;
}

/*
 *  Look up and resolve line numbers prior to running.
 *  Returns true iff line(s) are missing.
 *  Call with 'where' less than zero; it's the last wNUMBEREDLINE seen.
 */
int link(lego *l, double where)
{
    lego *find;
    int i, bad = 0;

    /* visit 'l' and its siblings */
    for (; l; l = l -> next) {

        /* be ready to show where the trouble is */
        if (l -> what == wNUMBEREDLINE)
            where = l -> n;

        /* visit children of 'l' */
        for (i = 0; i<max_args; i++)
            bad += link(l -> a[i], where);

        /* only line references need linked */
        if (l -> what != wLINENUM)
            continue;

        /* do the link */
        if (l -> n < 0)
            l -> link = NULL;
        else if (find = find_line(l -> n))
            l -> link = find;
        else {
            ++bad;
            flash('e');
            printf("can't find line %.0f", l -> n);
            if (where >= 0)
                printf(" in %.0f", where);
            printf("\n");
            flash('n');
            warn("~");
        }
    }

    return bad;
}

/*
 *  Modify all line links (GOTO ___, RESTORE ___, GOSUB ___, etc.)
 *  within a given statement to point to a place that might not be where
 *  the original source specified. Implements ALTER ___ TO PROCEED TO ___.
 *  Cannot be used to change ON ___ multi-way stuff.
 *  Is able to change line modified by ALTER.
 *  Returns zero for success.
 */
void do_alter(lego *vi, lego *de)
{
    int yes = 0;

    for (vi = vi -> link, vi = vi -> a[0]; vi; vi = vi -> next)
        switch (vi -> what) {
            case wGOTO:
            case wGOSUB:
            case wRESTORE:
            case wALTER:
                if (vi -> a[0])
                    yes = 1, vi -> a[0] -> link = de -> link;
        }
    if (!yes)
        warn("no alterations");
}

/*
 *  'l' is a wLINENUM that has a number at n, might have code at a[0].
 *  Insert that line in the program. If no code, just delete that line.
 */
void save_line(lego *l)
{
    lego **find, *bye;

    /*
     *  Linkage and the program context will no longer be reliable.
     */
    reset_program();

    /*
     *  Find where the line goes in the linked list.
     */
    for (find = &program; *find; find = &(*find) -> next)
        if ((*find) -> n >= l -> n)
            break;

    /*
     *  If that line number exists already, remove and free it.
     */
    if (*find && (*find) -> n == l -> n) {
        bye = *find;
        *find = bye -> next;
        bye -> next = NULL;
        byeLego(bye);
    }

    /*
     *  If nothing was deleted and 'l' has no code, user error. Free 'l'.
     */
    else if (!l -> a[0]) {
        warn("no such line to delete");
        goto l_bye;
    }

    /*
     *  If 'l' has code, add to the program.
     */
    if (l -> a[0]) {
        l -> next = *find;
        *find = l;
        return;
    }

    /*
     *  Otherwise, we need to free 'l' since it's not saved in a program.
     */
l_bye:
    byeLego(l);
}

/*
 *  List the program.
 */
void list(double vi, double de)
{
    int any = 0;
    lego *l;

    for (l = program; l; l = l -> next)
        if ((vi < 0 || l -> n >= vi) && (de < 0 || l -> n <= de)) {
            printLego(l);
            printf("\n");
            ++any;
        }

    if (!any && vi == de && vi >= 0)
        warn("no such line to list");
}

/*
 *  Delete identified lines.
 */
void del(double vi, double de)
{
    int any = 0;
    lego **l, *bye;

    for (l = &program; *l;) {
        if ((vi < 0 || (*l) -> n >= vi) && (de < 0 || (*l) -> n <= de)) {
            bye = *l;
            *l = bye -> next;
            bye -> next = NULL;
            byeLego(bye);
            ++any;
        }
        else
            l = &(*l) -> next;
    }

    if (!any && vi == de && vi >= 0)
        warn("no such line");
    else
        /*
         *  Linkage and the program context are no longer reliable.
         */
        reset_program();
}

/*
 *  Execute the PRINT statement. Returns true iff errors.
 */
int runPrint(lego *l)
{
    lego *loop;
    computed q;
    char *s;

    if (!l -> a[0]) printf("\n");
    for (loop = l -> a[0]; loop; loop = loop -> next) {
        q = evalloc(loop);
        switch (q.what) {
            case rString:
                printf("%s", q.s);
                for (s = q.s; *s; ++s)
                    dirty = *s != '\n';
                zap(q.s);
                break;
            case rNum:
                printf(trunc(q.n) == q.n ? "%.0f" : "%f", q.n);
                dirty = 1;
                break;
            case rExcept:
                return 1;
        }
        if (!loop -> list_delim) {
            printf(loop -> next ? " " : "\n");
            dirty = !!loop -> next;
        }
    }
    return 0;
}

/*
 *  Compel output to be on a line by itself.
 */
void byItself(void)
{
    if (!dirty) return;
    printf("\n");
    dirty = 0;
}

/*
 *  Print error or break message with possible line number.
 */
void advise(char *msg, int ran, double lNum)
{
    if (*msg == '~')
        return;             /* message(s) issued by linker */
    byItself();
    flash('e');
    printf("%s", msg);
    if (ran && lNum >= 0)
        printf(" in %.0f", lNum);
    printf("\n");
    flash('n');
}

/*
 *  This loops through entries in a program's DATA statements.
 */
lego *get_next_data(void)
{
    lego *l;

    /* Most simply, a datum is all queued up. */
requeue:
    if (l = prog_con.data_datum) {
        prog_con.data_datum = prog_con.data_datum -> next;
        return l;
    }

    /* Find DATA within this line's remaining statements. */
    for (; prog_con.data_stmt; prog_con.data_stmt = prog_con.data_stmt -> next)
        if (prog_con.data_stmt -> what == wDATA) {
            prog_con.data_datum = prog_con.data_stmt -> a[0];
            prog_con.data_stmt = prog_con.data_stmt -> next;
            goto requeue;
        }

    /* Find statements within numbered lines. */
    if (prog_con.data_line) {
        prog_con.data_stmt = prog_con.data_line -> a[0];
        prog_con.data_line = prog_con.data_line -> next;
        goto requeue;
    }

    return NULL;
}

/*
 *  Get values for INPUT statement.
 */
void get_inputs(lego *l, char *prompt)
{
    char *s;
    lego *redo_from = l, *var;

redo_from_start:
    l = redo_from;
    s = NULL;
    printf("%s", prompt);
    s = read_line();

    while (l) {
        if (!s) {           /* Ctrl-C restarts line; Ctrl-D aborts program */
            ctrl_c = 1;
            return;
        }
        if (l -> what == wSTRVAR) {
            /* even empty strings succeed */
            if (!str_lit(&s, &var))
                unquoted_str_lit(&s, &var);
            set_var(l -> s, var -> s, 0);
        }
        else {
            /* empty numbers keep prompting */
            if (nothingMore(&s)) {
                printf("? ");
                s = read_line();
                continue;
            }
            if (!num_lit(&s, &var))
                goto oops;
            set_var(l -> s, NULL, var -> n);
        }
        byeLego(var);
        l = l -> next;
        if (!l)
            break;
        if (nothingMore(&s)) {
            printf("? ");
            s = read_line();
        }
        else if (symbol(&s, ","))
            ;
        else
            goto oops;
    }
    if (s && !nothingMore(&s))
        goto oops;                  /* too many items input */
    return;

oops:
    flash('e');
    puts("redo from start");
    flash('n');
    goto redo_from_start;
}

/*
 *  This terminates nested FOR ... NEXT loops.
 */
void expire_next_stack(x_con *c, char *nVar, int inclusive)
{
    next_stack *ns;
    int i = !!inclusive;

    /* Count how many will be removed. */
    for (ns = c -> next_to; ns; ns = ns -> next, ++i)
        if (!nVar || !strcmp(ns -> nVar, nVar))
            goto yes;
    return;

    /* Remove everything up to and maybe including nVar */
yes:
    while (i--) {
        ns = c -> next_to -> next;
        zap(c -> next_to -> nVar);
        zap(c -> next_to);
        c -> next_to = ns;
    }
}

/*
 *  Advance /one/ step in whatever code we have.
 *  c -> code, line, stmt can be one and the same for immediate commands,
 *  in which case 'line' won't have a number.
 *
 *  This routine is agnostic as to whether a step /should/ be taken; e.g.,
 *  it doesn't think about whether a program is running or stopped. It
 *  merely proceeds one step. If the program has ended, 'line' and 'stmt'
 *  are NULL. It does not initialize 'line' or 'stmt' prior to execution;
 *  the caller needs to do this.
 *
 *  This routine does not catch typed program lines ("10 CLS"); the
 *  caller has already filtered these out. Nor does it handle DEL statements,
 *  as these also modify the program and would invalidate several invariants.
 *
 *  An integer "honey do" is returned to inform the caller of any action
 *  it may need to take.
 */
int single_step(x_con *c)
{
    lego *l, *dest, *datum;
    computed q;
    ret_stack *ret_to;
    next_stack *next_to;
    double xyz[4];
    int i;
    char *s;

    /* Get the current statement in 'l'. */
    while (!c -> stmt) {
        if (!c -> line)
            return wEND;
        c -> stmt = c -> line = c -> line -> next;
    }

    /* Default assumption: next statement is its sibling. */
    l = c -> stmt;
    c -> stmt = c -> stmt -> next;

    switch (l -> what) {

        case wRUN:
            start_at = l -> a[0] ? l -> a[0] -> link : NULL;
        case wNEW:
        case wEND:
        case wSTOP:
        case wCONT:
            /* Caller will handle these context adjustments. */
            return l -> what;

        case wNUMBEREDLINE:
            /* Next statement is child of this one. */
            c -> lNum = l -> n;
            c -> stmt = l -> a[0];
            break;

        case wLIST:
            list(l -> a[0] -> n, l -> a[0] -> next -> n);
            break;

        case wDEL:
            if (c == &prog_con) {
                warn("attempt to modify running program");
                return wERROR;
            }
            del(l -> a[0] -> n, l -> a[0] -> next -> n);
            break;

        case wCLS:
            flash('c');
            break;

        case wPRINT:
            if (runPrint(l))
                return wERROR;          /* exception in evaluation tree */
            break;

        case wIF:
            q = evalloc(l -> a[0]);
            if (q.what == rExcept)
                return wERROR;
            if (q.n) {
                if (l -> a[1])
                    c -> stmt = l -> a[1];
            }
            else if (l -> a[2])
                c -> stmt = l -> a[2];
            zap(q.s);                   /* parser ensures this isn't needed */
            break;

        case wLET:
            q = evalloc(l -> a[1]);
            if (q.what == rExcept)
                return wERROR;
            if (l -> a[0] -> what == wSTRVAR)
                set_var(l -> a[0] -> s, q.s, 0);
            else if (l -> a[0] -> what == wNUMVAR)
                set_var(l -> a[0] -> s, NULL, q.n);
            else {
                warn("assertion failed (check parser)");
                return wERROR;
            }
            zap(q.s);
            break;

        case wONGOTO:
            q = evalloc(l -> a[0]);
            if (q.what == rExcept)
                return wERROR;
            for (i = 1, dest = l -> a[1]; dest; ++i, dest = dest -> next)
                if (i == q.n)
                    goto like_GOTO;
            break;

        case wGOTO:
            dest = l -> a[0];
like_GOTO:
            if (c != &prog_con) {       /* immediate context? */
                start_at = dest -> link;
                return l -> what;
            }
            c -> stmt = c -> line = dest -> link;
            break;

        case wREM:
            break;          /* no-op */
 
        case wGOSUB:
            if (c != &prog_con) {       /* immediate context? */
                /*
                 *  Complex enough to skip implementing because:
                 *  GOSUB and RETURN would both switch context.
                 */
                warn("immediate GOSUB not supported");
                break;
            }
            dest = l -> a[0];
like_GOSUB:
            ret_to = getmem(sizeof(ret_stack));
            ret_to -> line = c -> line, ret_to -> stmt = c -> stmt;
            ret_to -> running = c == &prog_con;
            ret_to -> next = c -> ret_to;
            c -> ret_to = ret_to;
            c -> stmt = c -> line = dest -> link;
            break;

        case wONGOSUB:
            if (c != &prog_con) {       /* immediate context? */
                warn("immediate ON .. GOSUB not supported");
                break;
            }
            q = evalloc(l -> a[0]);
            if (q.what == rExcept)
                return wERROR;
            for (i = 1, dest = l -> a[1]; dest; ++i, dest = dest -> next)
                if (i == q.n)
                    goto like_GOSUB;
            break;

        case wRETURN:
            /*
             *  Immediate RETURN is supported even though immediate
             *  GOSUB isn't, as the former is not complex.
             */
            if (!prog_con.ret_to) {
                warn("RETURN without GOSUB");
                break;
            }
            ret_to = prog_con.ret_to;
            prog_con.line = ret_to -> line, prog_con.stmt = ret_to -> stmt;
            prog_con.ret_to = ret_to -> next;
            zap(ret_to);
            return wRETURN;

        case wRESTORE:
            prog_con.data_line = l -> a[0] ? l -> a[0] -> link : program;
            prog_con.data_stmt = prog_con.data_datum = NULL;
            break;

        case wDATA:
            break;          /* not executable */

        case wREAD:
            for (dest = l -> a[0]; dest; dest = dest -> next) {

                /* Get item from next available DATA */
                datum = get_next_data();
                if (!datum) {
                    warn("out of data");
                    break;
                }

                /* Our DATA is "dynamic" and can contain expressions! */
                q = evalloc(datum);
                if (q.what == rExcept)
                    return wERROR;

                /* Read string variable. */
                if (dest -> what == wSTRVAR) {
                    if (q.what != rString)
                        warn("type mismatch");
                    else
                        set_var(dest -> s, q.s, 0);
                    zap(q.s);
                }

                /* Read numeric variable. */
                else {
                    if (q.what != rNum) {
                        warn("type mismatch");
                        break;
                    }
                    set_var(dest -> s, NULL, q.n);
                }
            }
            break;

        case wINPUT:
            get_inputs(l -> a[1], l -> a[0] ? l -> a[0] -> s : "? ");
            break;

        case wLINEINPUT:
            s = read_line();
            if (s)
                set_var(l -> a[0] -> s, s, 0);
            else
                ctrl_c = 1;
            break;

        case wFOR:
            /*
             *  FOR and NEXT do not support context switching or
             *  play sanely with GOSUB and RETURN.
             *  Begin by evaluating the numeric parameters.
             */
            for (i=1; i<4; i++) {
                if (!l -> a[i]) {       /* i == 3 might have no STEP */
                    xyz[i] = 1;
                    continue;
                }
                q = evalloc(l -> a[i]);
                if (q.what != rNum)
                    return wERROR;
                xyz[i] = q.n;
            }

            /*  If we're already looping on this variable, presume
             *  the old loop (and any under it) to be defunct.
             *  Then save new loop information and variable starting value.
             */
            expire_next_stack(c, l -> a[0] -> s, 1);
            next_to = getmem(sizeof(next_stack));
            next_to -> nVar = copySubstring(l -> a[0] -> s, NULL);
            next_to -> vi = xyz[1];
            next_to -> de = xyz[2];
            next_to -> step = xyz[3];
            next_to -> line = c -> line;
            next_to -> stmt = c -> stmt;
            next_to -> next = c -> next_to;
            c -> next_to = next_to;
            set_var(l -> a[0] -> s, NULL, xyz[1]);
            break;

        case wNEXT:
            s = l -> a[0] ? l -> a[0] -> s : NULL;
            expire_next_stack(c, s, 0);
            next_to = c -> next_to;
            if (!next_to || s && strcmp(s, next_to -> nVar)) {
                warn("NEXT without FOR");
                break;
            }
            q.n = num_from_name_hack(next_to -> nVar);
            q.n += next_to -> step;
            if (   next_to -> step > 0 && q.n > next_to -> de
                || next_to -> step < 0 && q.n < next_to -> de) {

                /* Loop has run its course. There is no NEXT. */
                zap(next_to -> nVar)
                c -> next_to = next_to -> next;
                zap(next_to);
                break;
            }

            /* Adjust variable and return to top of loop. */
            set_var(next_to -> nVar, NULL, q.n);
            c -> line = next_to -> line;
            c -> stmt = next_to -> stmt;
            break;

        case wALTER:
            do_alter(l -> a[0], l -> a[1]);
            break;

        case wONALTER:
            q = evalloc(l -> a[0]);
            if (q.what == rExcept)
                return wERROR;
            for (i = 1, dest = l -> a[2]; dest; ++i, dest = dest -> next)
                if (i == q.n) {
                    do_alter(l -> a[1], dest);
                    break;
                }
            break;

        default:
            flash('e');
            printf("unimplemented: %s\n", guys[l -> what]);
            flash('n');
    }

    return 0;
}

/*
 *  Handle a parsed immediate line.
 *  This can include entire program runs and more.
 */
int immediate(lego *l)
{
    x_con imm_con;
    int running = 0, ran, what;

    /*
     *  Case where user types a line into a program, like "10 CLS"
     */
    if (l -> what == wNUMBEREDLINE) {
        save_line(l);
        return 0;       /* We retained or freed 'l' in save_line() */
    }

    /*
     *  Resolve any line numbers in the immediate command.
     */
    if (link(l, -1))
        return 1;

    /*
     *  Fully initialize immediate context.
     */
    imm_con.line = NULL;
    imm_con.ret_to = NULL;
    imm_con.next_to = NULL;
    imm_con.stmt = l;
    imm_con.lNum = -1;

next_one:
    ran = running;
    switch (what = single_step(running ? &prog_con : &imm_con)) {

        case wRUN:
        case wGOTO:
        case wONGOTO:
            /* Link, initialize, and start program. */
            if (link(program, -1))
                break;

            /* LET, GOSUB, DATA, FOR left as-is if we used GOTO. */
            if (what == wRUN) {
                erase_run_vars();
                reset_program();
            }

            prog_con.line = prog_con.stmt = start_at ? start_at : program;
            running = 1;
            break;

        case wNEW:
            /* Erase program; ensure immediate context. */
            erase_program();
            running = 0;
            break;

        case wEND:
            if (!running) {
                /* Consider immediate statements aborted. */
                byItself();
                return 1;
            }

            /* Program ended. Keep vars, but clear context. */
            reset_program();
            running = 0;
            break;

        case wSTOP:
            /* Fake a CTRL-C. */
            ctrl_c = -1;
            break;

        case wCONT:
            /* Force program to have context. */
            if (!prog_con.line)
                warn("can't continue");
            else
                running = 1;
            break;

        case wRETURN:
            /* Immediate return will require: */
            running = 1;
            break;
    }

    /* Output error messages. */
    if (warning) {
        advise(warning, ran, prog_con.lNum);
        warning = NULL;
        if (ran)
            reset_program();
        else {
            imm_con.line = imm_con.stmt = NULL;
            imm_con.ret_to = NULL;
            imm_con.next_to = NULL;
        }
    }

    /* Handle SIGINT from user and STOP within program. */
    if (ctrl_c) {
        if (ctrl_c > 0)
            printf("\n");
        advise("break", ran, prog_con.lNum);
        running = 0;
        ctrl_c = 0;
    }

    /* Seek out another statement to execute. */
    goto next_one;
}
