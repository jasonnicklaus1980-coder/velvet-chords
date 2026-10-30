/* A small XML reader for element trees: nested elements, attributes, text, self-closing tags, comments and the
 * declaration. No DTDs, namespaces or processing instructions beyond <?...?>. Plain C, libc only. */
#ifndef VC_XML_MIN_H
#define VC_XML_MIN_H
#include <stddef.h>

typedef struct XNode XNode;
struct XNode {
    char *name;
    char *text;            /* text of this element (entities decoded, whitespace kept); "" if none */
    char **attr_names, **attr_values;
    int nattrs;
    XNode *child, *next;   /* first child and next sibling */
};

/* Parses a complete document. Returns the root element, or NULL if the text isn't well formed. Free with xml_free. */
XNode *xml_parse(const char *s, size_t len);
void xml_free(XNode *n);

const XNode *xml_child(const XNode *n, const char *name);          /* first child with this name, or NULL */
const char *xml_attr(const XNode *n, const char *name);            /* attribute value, or NULL */
const char *xml_child_text(const XNode *n, const char *name);      /* text of the first matching child, or NULL */

#endif
