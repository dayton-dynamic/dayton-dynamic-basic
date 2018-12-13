/*
    Dayton Dynamic BASIC
    expression evaluator
    MWA 2018

    See all.h for a list of unimplemented features.
*/

#include "all.h"

/*
 *  How variables are stored.
 */
typedef struct varDB {
    char *name;
    char *s;
    double n;
    struct varDB *next;
} varDB;

varDB *strDB, *numDB;

/*
 *  Provision for random number generation.
 *  If zero is passed for 'x', returns previous number generated,
 *  otherwise obtains a new one.
 */
double csprng(double x)
{
    static double last_rand = 0;
    union {
        char stuff[sizeof(unsigned)];
        unsigned u;
    } rBuf;

    if (!x || !urandom || 1 != fread(rBuf.stuff, sizeof(unsigned), 1, urandom))
        return last_rand;
    last_rand = rBuf.u / (double) UINT_MAX;
    return last_rand;
}

/*
 *  Update or create named variable as stated.
 */
void set_var(char *name, char *s, double n)
{
    varDB **v = s ? &strDB : &numDB;

    for (; *v; v = &(*v) -> next) {
        if (!strcmp((*v) -> name, name))
            break;
    }
    if (!*v) {
        *v = getmem(sizeof(varDB));
        (*v) -> name = copySubstring(name, NULL);
    }

    if (s) {
        zap ((*v) -> s);
        (*v) -> s = copySubstring(s, NULL);
    }
    else
        (*v) -> n = n;
}

/*
 *  Remove all vars.
 */
void erase_run_vars(void)
{
    varDB *next;

    for (; strDB; strDB = next) {
        next = strDB -> next;
        zap(strDB -> name);
        zap(strDB -> s);
        zap(strDB);
    }

    for (; numDB; numDB = next) {
        next = numDB -> next;
        zap(numDB -> name);
        zap(numDB -> s);
        zap(numDB);
    }
}

/*
 *  Find a var.
 */
varDB *find_var(varDB *root, char *name)
{
    for (; root; root = root -> next)
        if (!strcmp(name, root -> name))
            return root;
    return NULL;
}

/*
 *  Get value of a numeric var that we know exists.
 */
double num_from_name_hack(char *name)
{
    varDB *v;

    for (v = numDB; v; v = v -> next)
        if (!strcmp(name, v -> name))
            return v -> n;
    puts("bad hack");
    exit(1);
}

/*
 *  Boolean arithmetic ensuring reasonable bounds.
 */
computed boolean_logic(double x, double y, int what)
{
    computed res = { 0 };
    int xi = x, yi = y;

    if (xi != x || yi != y) {
        warn("need integer");
        res.what = rExcept;
        return res;
    }

    switch (what) {
        case wAND:
            xi = xi & yi;
            break;
        case wOR:
            xi = xi | yi;
            break;
        case wXOR:
            xi = xi ^ yi;
            break;
        case wEQV:
            xi = ~(xi ^ yi);
            break;
        case wIMP:
            xi = ~xi | yi;
            break;
        case wNAND:
            xi = ~(xi & yi);
        case wNOR:
            xi = ~(xi | yi);
    }

    res.what = rNum;
    res.n = xi;
    return res;
}

/*
 *  Integer division and modulus with reasonable bounds.
 */
computed divmod(double x, double y, int what)
{
    computed res = { 0 };
    int xi = x, yi = y;

    if (xi != x || yi != y) {
        warn("need integer");
        res.what = rExcept;
        return res;
    }

    switch (what) {
        case wIDIV:
            xi /= yi;
            break;
        case wMOD:
            xi %= yi;
            break;
    }

    res.what = rNum;
    res.n = xi;
    return res;
}

/*
 *  Evaluate a string or numeric expression, returning a newly
 *  allocated string literal or numeric literal.
 */
computed evalloc(lego *l)
{
    enum { max_str = 32 };
    int nargs = 0, i, j, len;
    int except = 0;
    char *s;
    varDB *v;
    computed q = { 0 }, x = { 0 }, y = { 0 }, z = { 0 };

    /* direct return of numbers and strings */
    switch (l -> what) {
        case wSTRLIT:
            q.what = rString;
            q.s = copySubstring(l -> s, NULL);
            return q;
        case wNUMLIT:
            q.what = rNum;
            q.n = l -> n;
            return q;
        case wSTRVAR:
        case wNUMVAR:
            v = find_var(l -> what == wSTRVAR ? strDB : numDB, l -> s);
            if (!v) {
                warn("no such variable");
                goto exception;
            }
            if (l -> what == wSTRVAR) {
                q.what = rString;
                q.s = copySubstring(v -> s, NULL);
            }
            else {
                q.what = rNum;
                q.n = v -> n;
            }
            return q;
    }

    /* figure out how many args */
    if (l -> what < END_UNARY_GUYS)
        nargs = 1;
    else if (l -> what < END_BINARY_GUYS)
        nargs = 2;
    else if (l -> what < END_FUNCTION_GUYS) {
        for (i=0; i<max_args; i++)
            if (l -> a[i])
                ++nargs;
    }
    else {
        warn("unimplemented evalloc");
        goto exception;
    }

    /* figure out return type */
    q.what = rNum;
    switch (l -> what) {
        case wCHR:
        case wLEFT:
        case wMID:
        case wRIGHT:
        case wSPACE:
        case wSTR:
        case wSTRING:
        case wCAT:
            q.what = rString;
    }

    /* get parameters as local variables (may have to free) */
    switch (nargs) {
        case 3:
            z = evalloc(l -> a[2]);
            if (z.what == rExcept)
                except = 1;
        case 2:
            y = evalloc(l -> a[1]);
            if (y.what == rExcept)
                except = 1;
        case 1:
            x = evalloc(l -> a[0]);
            if (x.what == rExcept)
                except = 1;
    }

    /* roll exceptions up the call stack */
    if (except)
        goto exception;

    /* do what we have to do */
    switch (l -> what) {

        case wNEGATE:
            q = evalloc(l -> a[0]);
            q.n = -q.n;
            break;

        case wNOT:
            q.n = !x.n;
            break;

        case wPOWER:
            q.n = pow(x.n, y.n);
            break;

        case wMUL:
            q.n = x.n * y.n;
            break;

        case wDIV:
            q.n = x.n / y.n;
            break;

        case wADD:
            q.n = x.n + y.n;
            break;

        case wSUB:
            q.n = x.n - y.n;
            break;

        case wGT:
            q.n = -(x.n > y.n);
            break;

        case wGE:
            q.n = -(x.n >= y.n);
            break;

        case wLT:
            q.n = -(x.n < y.n);
            break;

        case wLE:
            q.n = -(x.n <= y.n);
            break;

        case wEQ:
            q.n = -(x.n == y.n);
            break;

        case wNE:
            q.n = -(x.n != y.n);
            break;

        case wAND:
        case wOR:
        case wXOR:
        case wEQV:
        case wIMP:
        case wNAND:
        case wNOR:
            q = boolean_logic(x.n, y.n, l -> what);
            break;

        case wIDIV:
        case wMOD:
            q = divmod(x.n, y.n, l -> what);
            break;

        case wABS:
            q.n = fabs(x.n);
            break;

        case wASC:
            if (!*x.s) {
                warn("need non-empty string");
                goto exception;
            }
            q.n = *x.s;
            break;

        case wATAN:
            q.n = atan(x.n);
            break;

        case wCOS:
            q.n = cos(x.n);
            break;

        case wEXP:
            q.n = exp(x.n);
            break;

        case wFIX:
            q.n = trunc(x.n);
            break;

        case wINT:
            q.n = floor(x.n);
            break;

        case wLEN:
            q.n = strlen(x.s);
            break;

        case wLOG:
            q.n = log(x.n);
            break;

        case wRND:
            q.n = csprng(x.n);
            break;

        case wSGN:
            q.n = (x.n > 0) - (x.n < 0);
            break;

        case wSIN:
            q.n = sin(x.n);
            break;

        case wSQRT:
            q.n = sqrt(x.n);
            break;

        case wTAN:
            q.n = tan(x.n);
            break;

        case wINSTR:
            if (x.n != trunc(x.n) || x.n < 1 || x.n > 2147483646.) {
                warn("need positive integer");
                goto exception;
            }
            if (x.n > strlen(y.s))
                q.n = 0;
            else
                s = strstr(y.s + (int) x.n - 1, z.s), q.n = s ? s - y.s + 1 : 0;
            break;

        case wCHR:
            if (x.n != trunc(x.n) || x.n < 1 || x.n > 255) {
                warn("need integer within 1 to 255");
                goto exception;
            }
            s = getmem(2);
            s[0] = x.n, s[1] = '\0';
            q.s = s;
            break;

        case wSTR:
            s = getmem(max_str);
            snprintf(s, max_str, trunc(x.n) == x.n ? "%.0f" : "%f", x.n);
            q.s = s;
            break;

        case wCAT:
            s = getmem((i = strlen(x.s)) + strlen(y.s) + 1);
            strcpy(s, x.s);
            strcpy(s+i, y.s);
            q.s = s;
            break;

        /* TODO here thru MID$ works like TRS-80 but not like Python,
           in that we allow overlong but not underlong args. */
        case wSTRING:
        case wSPACE:
            if (x.n != trunc(x.n) || x.n < 0 || x.n > 2147483646.) {
                warn("need non-negative integer");
                goto exception;
            }
            i = ' ';
            if (l -> what == wSTRING) {
                if (!*y.s) {
                    warn("need non-empty string");
                    goto exception;
                }
                i = *y.s;
            }
            q.s = getmem(x.n + 1);
            memset(q.s, i, x.n);
            break;

        case wLEFT:
            if (y.n != trunc(y.n) || y.n < 0 || y.n > 2147483646.) {
                warn("need non-negative integer");
                goto exception;
            }
            i = strlen(x.s);
            i = i > y.n ? y.n : i;
            q.s = copySubstring(x.s, x.s + i);
            break;

        case wRIGHT:
            if (y.n != trunc(y.n) || y.n < 0 || y.n > 2147483646.) {
                warn("need non-negative integer for RIGHT$");
                goto exception;
            }
            i = strlen(x.s);
            i = i > y.n ? i - y.n : 0;
            q.s = copySubstring(x.s + i, NULL);
            break;

        case wMID:
            if (y.n != trunc(y.n) || y.n < 1 || y.n > 2147483646.) {
                warn("need positive integer");
                goto exception;
            }
            if (z.n != trunc(z.n) || z.n < 0 || z.n > 2147483646.) {
                warn("need non-negative integer");
                goto exception;
            }
            len = strlen(x.s);
            i = y.n - 1, j = z.n;
            if (i > len)
                i = len;
            if (i + j > len)
                j = len - i;
            q.s = copySubstring(x.s + i, x.s + i + j);
            break;

        default:
            warn("unimplemented in evalloc");
            goto exception;
    }
    goto done;


    /* handle exceptions */
exception:
    q.what = rExcept;

done:
    /* free intermediate results */
    switch (nargs) {
        case 3:
            if (z.s)
                zap(z.s);
        case 2:
            if (y.s)
                zap(y.s);
        case 1:;
            if (x.s)
                zap(x.s);
    }

    return q;
}
