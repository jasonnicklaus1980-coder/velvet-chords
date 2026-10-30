/* XML reader tests: nesting, attributes, entities, self-closing tags, comments, whitespace, and bad input. */
#include <stdio.h>
#include <string.h>
#include "xml_min.h"

static int failures = 0;
static void check(int ok, const char *what) { printf("  %s: %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++failures; }
static XNode *parse(const char *s) { return xml_parse(s, strlen(s)); }

int main(void) {
    printf("xml reader\n");
    XNode *r = parse("<?xml version=\"1.0\"?>\n<!-- top -->\n<A x=\"1\" y='two'>\n  <B>hello &amp; bye</B>\n  <C/>\n  <D n=\"3\"><E>keep  spaces </E></D>\n</A>\n");
    check(r != NULL, "a document with a declaration and a comment parses");
    check(r && strcmp(r->name, "A") == 0, "root is A");
    check(xml_attr(r, "x") && strcmp(xml_attr(r, "x"), "1") == 0, "attribute with double quotes");
    check(xml_attr(r, "y") && strcmp(xml_attr(r, "y"), "two") == 0, "attribute with single quotes");
    check(xml_attr(r, "missing") == NULL, "a missing attribute is NULL");
    check(xml_child_text(r, "B") && strcmp(xml_child_text(r, "B"), "hello & bye") == 0, "entities are decoded in text");
    check(xml_child(r, "C") && xml_child_text(r, "C") && xml_child_text(r, "C")[0] == 0, "self-closing tag has empty text");
    const XNode *d = xml_child(r, "D");
    check(d && strcmp(xml_attr(d, "n"), "3") == 0, "nested element keeps its attribute");
    check(xml_child_text(d, "E") && strcmp(xml_child_text(d, "E"), "keep  spaces ") == 0, "text keeps its inner and trailing spaces");
    check(xml_child(r, "nope") == NULL, "a missing child is NULL");
    xml_free(r);

    printf("bad input\n");
    check(parse("<A><B></A>") == NULL, "mismatched closing tag is refused");
    check(parse("<A>") == NULL, "unclosed element is refused");
    check(parse("<A x=1/>") == NULL, "unquoted attribute value is refused");
    check(parse("") == NULL, "empty input is refused");
    check(parse("<A/><B/>") == NULL, "two root elements are refused");

    printf("\n%s\n", failures ? "XML TESTS FAILED" : "xml tests passed");
    return failures ? 1 : 0;
}
