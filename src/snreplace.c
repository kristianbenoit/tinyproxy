/* tinyproxy - A fast light-weight HTTP proxy
 * Copyright (C) 1999 George Talusan <gstalusan@uwaterloo.ca>
 * Copyright (C) 2002 James E. Flemer <jflemer@acm.jhu.edu>
 * Copyright (C) 2002 Robert James Kaes <rjkaes@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* The search and replace regex as @search@replace goes into the file
 * pointed at by DEFAULT_SNREPLACE.
 */

#include "main.h"

#include "snreplace.h"
#include "heap.h"
#include "log.h"
#include "reqs.h"
#include "conf.h"

#define SNREPLACE_BUFFER_LEN (512)

static int err;

struct snreplace_list {
        struct snreplace_list *next;
        char *pat;
        char *sub;
        regex_t *cpat;
};

static struct snreplace_list *rl = NULL;
static int already_init = 0;

static size_t snreplace_paste_match(
        const struct snreplace_list *snreplace,
        const char *url_in,
        char *url_out,
        size_t out_len,
        const regmatch_t *match,
        char matchid
        );

static int snreplace_do_substitution(
        const struct snreplace_list *snreplace,
        const char *url_in,
        char *url_out,
        size_t out_len,
        const regmatch_t *match
        );
/*
 * Initializes a linked list of strings containing hosts/urls to be snreplaceed
 */
void snreplace_init (void)
{
        FILE *fd;
        struct snreplace_list *p;
        char buf[SNREPLACE_BUFFER_LEN];
        char *s;
        char *sub;
        char sep;
        int cflags;

        if (rl || already_init) {
                return;
        }

        fd = fopen (config.snreplace, "r");
        if (!fd) {
                return;
        }

        p = NULL;

        cflags = REG_NEWLINE;
        if (config.filter_extended)
                cflags |= REG_EXTENDED;
        if (!config.filter_casesensitive)
                cflags |= REG_ICASE;

        while (fgets (buf, SNREPLACE_BUFFER_LEN, fd)) {
                /*
                 * Remove any trailing white space and
                 * comments.
                 */
                s = buf;
                while (*s) {
                        if (isspace ((unsigned char) *s))
                                break;
                        if (*s == '#') {
                                /*
                                 * If the '#' char is preceeded by
                                 * an escape, it's not a comment
                                 * string.
                                 */
                                if (s == buf || *(s - 1) != '\\')
                                        break;
                        }
                        ++s;
                }
                *s = '\0';

                /* skip leading whitespace */
                s = buf;
                while (*s && isspace ((unsigned char) *s))
                        s++;

                /* skip blank lines and comments */
                if (*s == '\0')
                        continue;

                /* Find the second occurence of the first character,
                 * the separator between the regex and the substitution.
                 */
                sep = *s++;
                for (sub = s ; *sub ; sub++) {
                    if (*sub == sep) {
                        *sub++ = '\0';
                        break;
                    }
                }

                if (!p) /* head of list */
                        rl = p =
                            (struct snreplace_list *)
                            safecalloc (1, sizeof (struct snreplace_list));
                else {  /* next entry */
                        p->next =
                            (struct snreplace_list *)
                            safecalloc (1, sizeof (struct snreplace_list));
                        p = p->next;
                }

                p->pat = safestrdup (s);
                p->sub = safestrdup (sub);
                p->cpat = (regex_t *) safemalloc (sizeof (regex_t));
                err = regcomp (p->cpat, p->pat, cflags);
                if (err != 0) {
                        fprintf (stderr,
                                 "Bad regex in %s: %s\n",
                                 config.snreplace, p->pat);
                        exit (EX_DATAERR);
                }
                DEBUG2("Adding search and replace %s -> %s", p->pat, p->sub);
        }
        if (ferror (fd)) {
                perror ("fgets");
                exit (EX_DATAERR);
        }
        fclose (fd);

        already_init = 1;
}

/* unlink the list */
void snreplace_destroy (void)
{
        struct snreplace_list *p, *q;

        if (already_init) {
                for (p = q = rl; p; p = q) {
                        regfree (p->cpat);
                        safefree (p->cpat);
                        safefree (p->pat);
                        safefree (p->sub);
                        q = p->next;
                        safefree (p);
                }
                rl = NULL;
                already_init = 0;
        }
}

/**
 * reload the snreplace file if snreplaceing is enabled
 */
void snreplace_reload (void)
{
        if (config.snreplace) {
                log_message (LOG_NOTICE, "Re-reading snreplace file.");
                snreplace_destroy ();
                snreplace_init ();
        }
}

/* return the size pasted or -1 on error */
static size_t snreplace_paste_match(const struct snreplace_list *snreplace, const char *url_in, char *url_out, size_t out_len, const regmatch_t *match, char matchid) {
    size_t mlen = match->rm_eo - match->rm_so;
    if (match->rm_so < 0) {
        log_message (LOG_WARNING, "Invalid substitution string \"s@%s@%s@\""
                ", no match %c.", snreplace->pat, snreplace->sub, matchid);
        return -1;
    }
    if (mlen >= out_len) {
        log_message (LOG_WARNING, "Invalid substitution string \"s@%s@%s@\""
                ", result would be too long.", snreplace->pat, snreplace->sub);
        return -1;
    }
    memcpy(url_out, url_in + match->rm_so, mlen);
    return mlen;
}

static int snreplace_do_substitution(const struct snreplace_list *snreplace, const char *url_in, char *url_out, size_t out_len, const regmatch_t *match) {
    char *outp = url_out;
    char *out_endp = url_out + out_len - 1;
    char *subp;
    char nextc;
    int len;
    regmatch_t m;

    /* Copy before the whole match */
    m.rm_so=0;
    m.rm_eo=match[0].rm_so;
    len = snreplace_paste_match(snreplace, url_in, outp, out_endp - outp, &m, '-');
    if (len < 0)
        return len;
    outp+=len;

    /* Substitution */
    for (subp=snreplace->sub ; *subp ; subp++) {
        DEBUG2("outp = %s", url_out);
        if (*subp == '\\') {
            nextc = *(++subp);
            if (nextc >= '1' && nextc <= '9') {
                len = snreplace_paste_match(snreplace, url_in, outp, out_endp - outp, match + (nextc-'0'), nextc);
                if (len < 0) {
                    DEBUG1("len < 0");
                    return len;
                }
                outp+=len;
            } else
                *(outp++) = nextc;
        } else if (*subp == '&') {
            len = snreplace_paste_match(snreplace, url_in, outp, out_endp - outp, match, *subp);
            if (len < 0)
                return len;
            outp+=len;
        } else
            *(outp++) = *subp;
    }

    /* Copy after the whole match*/
    m.rm_so = match[0].rm_eo;
    m.rm_eo = strlen(url_in);
    len = snreplace_paste_match(snreplace, url_in, outp, out_endp - outp, &m, '-');
    if (len < 0)
        return len;
    outp+=len;

    *outp = '\0';
    return 0;
}

int snreplace_url (const char *url_in, char *url_out, size_t out_len)
{
        struct snreplace_list *p;
        int result;
        int foundmatch = 0;
        regmatch_t match[10];

        if (!rl || !already_init)
                return 0;

        for (p = rl; p; p = p->next) {
                result =
                    regexec (p->cpat, url_in, 10, match, 0);
                DEBUG2("Result for regexec(%s, %s) is %i", p->pat, url_in, result );
                if (result == 0) {
                    int i;
                    for (i = 0 ; i<10; i++)
                        DEBUG2("match[%i] = %i, %i", i, match[i].rm_so, match[i].rm_eo);

                    if (snreplace_do_substitution(p, url_in, url_out, out_len, match)) {
                        return 0;
                    }
                    foundmatch = 1;
                    break;
                }
        }

        return foundmatch;
}
