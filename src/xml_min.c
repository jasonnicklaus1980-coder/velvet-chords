#include "xml_min.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const char *s; size_t n, i; } Cur;

static void skip_ws(Cur *c) { while (c->i < c->n && (c->s[c->i] == ' ' || c->s[c->i] == '\t' || c->s[c->i] == '\r' || c->s[c->i] == '\n')) c->i++; }

static char *dup_n(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

/* decodes the five predefined entities into a new string */
static char *decode(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '&') {
            size_t e = i + 1;
            while (e < n && e - i < 8 && s[e] != ';') e++;
            if (e < n && s[e] == ';') {
                size_t l = e - i - 1;
                const char *k = s + i + 1;
                if (l == 3 && !memcmp(k, "amp", 3)) { d[o++] = '&'; i = e + 1; continue; }
                if (l == 2 && !memcmp(k, "lt", 2)) { d[o++] = '<'; i = e + 1; continue; }
                if (l == 2 && !memcmp(k, "gt", 2)) { d[o++] = '>'; i = e + 1; continue; }
                if (l == 4 && !memcmp(k, "quot", 4)) { d[o++] = '"'; i = e + 1; continue; }
                if (l == 4 && !memcmp(k, "apos", 4)) { d[o++] = '\''; i = e + 1; continue; }
            }
        }
        d[o++] = s[i++];
    }
    d[o] = 0;
    return d;
}

static int skip_misc(Cur *c) {   /* declarations, processing instructions and comments before or between elements */
    for (;;) {
        skip_ws(c);
        if (c->i + 1 < c->n && c->s[c->i] == '<' && (c->s[c->i + 1] == '?' || c->s[c->i + 1] == '!')) {
            const char *end = c->s[c->i + 1] == '?' ? "?>" : ">";
            if (c->i + 4 <= c->n && !memcmp(c->s + c->i, "<!--", 4)) end = "-->";
            size_t j = c->i + 2;
            while (j + strlen(end) <= c->n && memcmp(c->s + j, end, strlen(end)) != 0) j++;
            if (j + strlen(end) > c->n) return -1;
            c->i = j + strlen(end);
            continue;
        }
        return 0;
    }
}

static int name_char(char ch) { return ch && ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '/' && ch != '>' && ch != '=' && ch != '<'; }

static XNode *parse_element(Cur *c);

static XNode *parse_element(Cur *c) {
    if (c->i >= c->n || c->s[c->i] != '<') return NULL;
    c->i++;
    size_t ns = c->i;
    while (c->i < c->n && name_char(c->s[c->i])) c->i++;
    if (c->i == ns) return NULL;
    XNode *node = calloc(1, sizeof *node);
    if (!node) return NULL;
    node->name = dup_n(c->s + ns, c->i - ns);
    node->text = dup_n("", 0);
    if (!node->name || !node->text) goto fail;

    /* attributes */
    for (;;) {
        skip_ws(c);
        if (c->i >= c->n) goto fail;
        if (c->s[c->i] == '/' || c->s[c->i] == '>') break;
        size_t an = c->i;
        while (c->i < c->n && name_char(c->s[c->i])) c->i++;
        if (c->i == an) goto fail;
        char *aname = dup_n(c->s + an, c->i - an);
        skip_ws(c);
        if (c->i >= c->n || c->s[c->i] != '=') { free(aname); goto fail; }
        c->i++;
        skip_ws(c);
        if (c->i >= c->n || (c->s[c->i] != '"' && c->s[c->i] != '\'')) { free(aname); goto fail; }
        char q = c->s[c->i++];
        size_t vs = c->i;
        while (c->i < c->n && c->s[c->i] != q) c->i++;
        if (c->i >= c->n) { free(aname); goto fail; }
        char *aval = decode(c->s + vs, c->i - vs);
        c->i++;
        if (!aname || !aval) { free(aname); free(aval); goto fail; }
        char **nn = realloc(node->attr_names, sizeof(char *) * (size_t)(node->nattrs + 1));
        char **vv = realloc(node->attr_values, sizeof(char *) * (size_t)(node->nattrs + 1));
        if (!nn || !vv) { free(aname); free(aval); goto fail; }
        node->attr_names = nn; node->attr_values = vv;
        node->attr_names[node->nattrs] = aname;
        node->attr_values[node->nattrs] = aval;
        node->nattrs++;
    }
    if (c->s[c->i] == '/') {                       /* self-closing */
        c->i++;
        if (c->i >= c->n || c->s[c->i] != '>') goto fail;
        c->i++;
        return node;
    }
    c->i++;                                        /* '>' */

    /* content: text, children, comments, until the closing tag */
    XNode **tail = &node->child;
    size_t tstart = c->i;
    size_t tlen = 0;
    char *acc = dup_n("", 0);
    if (!acc) goto fail;
    for (;;) {
        if (c->i >= c->n) { free(acc); goto fail; }
        if (c->s[c->i] == '<') {
            if (c->i + 1 < c->n && c->s[c->i + 1] == '/') break;
            if (c->i + 4 <= c->n && !memcmp(c->s + c->i, "<!--", 4)) {
                size_t j = c->i + 4;
                while (j + 3 <= c->n && memcmp(c->s + j, "-->", 3) != 0) j++;
                if (j + 3 > c->n) { free(acc); goto fail; }
                c->i = j + 3;
                continue;
            }
            XNode *ch = parse_element(c);
            if (!ch) { free(acc); goto fail; }
            *tail = ch; tail = &ch->next;
            continue;
        }
        c->i++;
        tlen = c->i - tstart;
    }
    /* the text is everything between the start tag and the first child-free part; keep it raw and decoded */
    {
        size_t tn = tlen;
        char *dec = decode(c->s + tstart, tn);
        free(acc);
        if (!dec) goto fail;
        int has_child = node->child != NULL;
        if (!has_child) { free(node->text); node->text = dec; }
        else free(dec);
    }
    /* closing tag */
    if (c->i + 1 >= c->n || c->s[c->i + 1] != '/') goto fail;
    c->i += 2;
    size_t cn = c->i;
    while (c->i < c->n && name_char(c->s[c->i])) c->i++;
    if (c->i - cn != strlen(node->name) || memcmp(c->s + cn, node->name, c->i - cn) != 0) goto fail;
    skip_ws(c);
    if (c->i >= c->n || c->s[c->i] != '>') goto fail;
    c->i++;
    return node;

fail:
    xml_free(node);
    return NULL;
}

XNode *xml_parse(const char *s, size_t len) {
    Cur c = {s, len, 0};
    if (skip_misc(&c) != 0) return NULL;
    XNode *root = parse_element(&c);
    if (!root) return NULL;
    if (skip_misc(&c) != 0 || c.i != c.n) { xml_free(root); return NULL; }
    return root;
}

void xml_free(XNode *n) {
    if (!n) return;
    xml_free(n->child);
    xml_free(n->next);
    free(n->name);
    free(n->text);
    for (int i = 0; i < n->nattrs; i++) { free(n->attr_names[i]); free(n->attr_values[i]); }
    free(n->attr_names);
    free(n->attr_values);
    free(n);
}

const XNode *xml_child(const XNode *n, const char *name) {
    if (!n) return NULL;
    for (const XNode *c = n->child; c; c = c->next) if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

const char *xml_attr(const XNode *n, const char *name) {
    if (!n) return NULL;
    for (int i = 0; i < n->nattrs; i++) if (strcmp(n->attr_names[i], name) == 0) return n->attr_values[i];
    return NULL;
}

const char *xml_child_text(const XNode *n, const char *name) {
    const XNode *c = xml_child(n, name);
    return c ? c->text : NULL;
}
