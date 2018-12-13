/*
    Dayton Dynamic BASIC
    printing (opposite of parsing)
    MWA 2018
*/

#include "all.h"

char *guys[] = { GUYS };        /* names of lego types */
int forceParens = 0;            /* show explicit parse tree */

void printLego(lego *l)
{
    int parens;
    int i;
    double a, b;
    lego *loop;

    if (!l) {
        printf("NULL!");
        return;
    }

    /*
     *  Although I avoid redundant parentheses like rap music, some
     *  users like to use them for "clarity." Therefore, the parser
     *  specifically marks where parentheses have been used, so I
     *  can now print them here.
     */
    parens = l -> what < END_BINARY_GUYS
        && (l -> force_parens || forceParens);
    if (parens)
        printf("(");

    /*
     *  Unary operations are recursively printed.
     */
    if (l -> what < END_UNARY_GUYS) {
        printf("%s", guys[l -> what]);
        printLego(l -> a[0]);
    }

    /*
     *  Binary operations are recursively printed.
     */
    else if (l -> what < END_BINARY_GUYS) {
        printLego(l -> a[0]);
        printf(" %s ", guys[l -> what]);
        printLego(l -> a[1]);
    }

    /*
     *  Functions and their arguments are recursively printed.
     */
    else if (l -> what < END_FUNCTION_GUYS) {
        printf("%s(", guys[l -> what]);
        for (i=0; i<max_args && l -> a[i]; i++) {
            if (i)
                printf(", ");
            printLego(l -> a[i]);
        }
        printf(")");
    }

    /*
     *  Other cases include variables, literals, and BASIC statements.
     *  I group these not by what they're used for in BASIC, but by 
     *  what code is needed to print them.
     */
    else switch (l -> what) {

        case wSTRVAR:
            printf("%s$", l -> s);
            break;

        case wSTRLIT:
            if (l -> lit_delim)
                printf("]%c%s%c", l -> lit_delim, l -> s, l -> lit_delim);
            else
                printf("[%s]", l -> s);
            break;

        case wNUMVAR:
            printf("%s", l -> s);
            break;

        case wNUMLIT:
        case wLINENUM:
        case wNUMBEREDLINE:
            printf(trunc(l -> n) == l -> n ? "%.0f" : "%f", l -> n);
            if (l -> what == wNUMBEREDLINE && l -> a[0]) {
                printf(" ");
                printLego(l -> a[0]);
            }
            break;

        case wREM:
            printf("%s %s", l -> abbrev ? "'" : "REM", l -> s);
            break;

        case wNEW:
        case wEND:
        case wSTOP:
        case wCONT:
        case wRETURN:
        case wCLS:
            printf("%s", guys[l -> what]);
            break;

        case wLIST:
        case wDEL:
            printf("%s", guys[l -> what]);
            a = l -> a[0] -> n;
            b = l -> a[0] -> next -> n;
            if (a < 0 && b < 0)
                ;
            else if (a == b)
                printf(" %.0f", a);
            else if (a < 0)
                printf(" -%.0f", b);
            else if (b < 0)
                printf(" %.0f-", a);
            else
                printf(" %.0f-%.0f", a, b);
            break;

        case wGOSUB:
        case wGOTO:
        case wRUN:
        case wRESTORE:
            printf("%s", guys[l -> what]);
            if (l -> a[0])
                printf(" %.0f", l -> a[0] -> n);
            break;

        case wONGOTO:
        case wONGOSUB:
            printf("ON ");
            printLego(l -> a[0]);
            printf(l -> what == wONGOTO ? " GOTO " : " GOSUB ");
            for (loop = l -> a[1]; loop; loop = loop -> next) {
                printLego(loop);
                if (loop -> next)
                    printf(", ");
            }
            break;

        case wFOR:
            printf("FOR ");
            printLego(l -> a[0]);
            printf(" = ");
            printLego(l -> a[1]);
            printf(" TO ");
            printLego(l -> a[2]);
            if (l -> a[3]) {
                printf(" STEP ");
                printLego(l -> a[3]);
            }
            break;

        case wNEXT:
            printf("NEXT");
            if (l -> a[0]) {
                printf(" ");
                printLego(l -> a[0]);
            }
            break;

        case wIF:
            printf("IF ");
            printLego(l -> a[0]);
            printf(" THEN ");
            printLego(l -> a[1]);
            if (l -> a[2]) {
                printf(" ELSE ");
                printLego(l -> a[2]);
            }
            break;

        case wREAD:
        case wDATA:
            printf("%s ", guys[l -> what]);
            for (loop = l -> a[0]; loop; loop = loop -> next) {
                printLego(loop);
                if (loop -> next)
                    printf(", ");
            }
            break;

        case wLET:
            if (!l -> abbrev)
                printf("LET ");
            printLego(l -> a[0]);
            printf(" = ");
            printLego(l -> a[1]);
            break;

        case wLINEINPUT:
            printf("LINE INPUT ");
            printLego(l -> a[0]);
            break;

        case wPRINT:
            printf("%s", l -> abbrev ? "?" : "PRINT");
            for (loop = l -> a[0]; loop; loop = loop -> next) {
                printf(" ");
                printLego(loop);
                if (loop -> list_delim)
                    printf(";");
                else if (loop -> next)
                    printf(",");
            }
            break;

        case wINPUT:
            printf("INPUT ");
            if (l -> a[0]) {
                printLego(l -> a[0]);
                printf("; ");
            }
            for (loop = l -> a[1]; loop; loop = loop -> next) {
                printLego(loop);
                if (loop -> next)
                    printf(", ");
            }
            break;

        case wALTER:
            printf("ALTER ");
            printLego(l -> a[0]);
            printf(" TO ");
            if (!l -> abbrev)
                printf("PROCEED TO ");
            printLego(l -> a[1]);
            break;

        case wONALTER:
            printf("ON ");
            printLego(l -> a[0]);
            printf(" ALTER ");
            printLego(l -> a[1]);
            printf(" TO ");
            if (!l -> abbrev)
                printf("PROCEED TO ");
            for (loop = l -> a[2]; loop; loop = loop -> next) {
                printLego(loop);
                if (loop -> next)
                    printf(", ");
            }
            break;

        default:
            printf("*UNIMPLEMENTED*");

    }

    if (parens)
        printf(")");

    /*
     *  Colon-separated chains of statements on the same line are printed
     *  using tail recursion.
     */
    if (l -> what > END_FUNCTION_GUYS 
            && l -> what < END_STATEMENT_GUYS && l -> next) {
        printf(": ");
        printLego(l -> next);
    }
}
