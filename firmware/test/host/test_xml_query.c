/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe */
#include "tinytest.h"
#include "xml_query.h"
#include <string.h>

#define CHILD(d, p, k, f) xml_query_child(d, (int)strlen(d), p, k, (int)strlen(k), f)
#define ATTR(d, n, k, o, c, l) xml_query_attr(d, (int)strlen(d), n, k, (int)strlen(k), o, c, l)

int main(void)
{
    const char *doc =
        "<?xml version='1.0'?><root><A x='first'/><B><A x=''/></B><A x='last'/></root>";
    char out[32];
    CHECK(CHILD(doc, -1, "root", 0) == 0);
    CHECK(CHILD(doc, 0, "A", 0) == 1);
    CHECK(CHILD(doc, 0, "A", 2) == 4);
    CHECK(CHILD(doc, 2, "A", 0) == 3);
    CHECK(CHILD(doc, 0, "A", 5) == -1);
    CHECK(xml_query_child(doc, strlen(doc), 0, NULL, 0, 2) == 2);
    CHECK(ATTR(doc, 4, "x", out, sizeof out, 0) == 1 && !strcmp(out, "last"));
    CHECK(ATTR(doc, 3, "x", out, sizeof out, 0) == 1 && !*out);
    CHECK(ATTR(doc, 0, "x", out, sizeof out, 0) == 0);
    CHECK(ATTR("<r a='x' a='y'/>", 0, "a", out, sizeof out, 0) == -1);
    CHECK(ATTR("<r x='&amp;&lt;&gt;&apos;&quot;'/>", 0, "x", out, sizeof out, 0) == 1);
    CHECK_STR(out, "&<>'\"");
    CHECK(ATTR("<r x='long&amp;&unknown;'/>", 0, "x", out, 5, 1) == -1);
    CHECK(ATTR("<r x='ЖЖЖЖ'/>", 0, "x", out, 5, 0) == -1);
    CHECK(ATTR("<r x='ЖЖЖЖ'/>", 0, "x", out, 6, 1) == 1 && !strcmp(out, "Ж..."));
    CHECK(ATTR("<r x='ЖЖЖЖ'/>", 0, "x", out, 5, 1) == 1 && !strcmp(out, "..."));
    CHECK(ATTR("<r x='ЖЖЖЖ'/>", 0, "x", out, 2, 1) == 1 && !*out);
    CHECK(ATTR("<r x='ЖЖЖЖ'/>", 0, "x", out, 3, 1) == 1 && !strcmp(out, "Ж"));
    CHECK(ATTR("<r x='abc'/>", 0, "x", out, 1, 1) == 1 && !*out);
    const char *bad[] = {"",
                         "<r>",
                         "<r/><x/>",
                         "<r></z>",
                         "<r a='abc/>",
                         "<r a='x'b='y'/>",
                         "<!DOCTYPE x><x/>",
                         "<r>text</r>",
                         "<r x='\xc0\x80'/>",
                         "<r x='\xed\xa0\x80'/>",
                         "<r x='\xf4\x90\x80\x80'/>",
                         "<r x='&#1;'/> trailing",
                         "<?xml wrong?><r/>",
                         "<?xml version='1.0' encoding='UTF-16'?><r/>",
                         "<?xml version='1.0' version='1.0'?><r/>",
                         "<?xml version='1.0' standalone='maybe'?><r/>",
                         "<r /> <"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK(CHILD(bad[i], -1, "r", 0) == -2);
        CHECK(ATTR(bad[i], 0, "x", out, sizeof out, 0) == -1);
    }
    CHECK(CHILD("<?xml version='1.0' encoding='utf-8' standalone='yes'?><r/>", -1, "r", 0) == 0);
    CHECK(ATTR("<r/>", 1, "x", out, sizeof out, 0) == -1);
    strcpy(out, "keep");
    CHECK(ATTR("<r x='long&amp;&unknown;'/>", 0, "x", out, 5, 1) == -1 && !strcmp(out, "keep"));
    CHECK(ATTR("<r x='longer'/>", 0, "x", out, 2, 0) == -1 && !strcmp(out, "keep"));
    CHECK(ATTR("<r/>", 0, "x", out, 2, 0) == 0 && !strcmp(out, "keep"));
    char changed[] = "<r x='a'/>";
    CHECK(ATTR(changed, 0, "x", out, sizeof out, 0) == 1 && out[0] == 'a');
    changed[6] = 'b';
    CHECK(ATTR(changed, 0, "x", out, sizeof out, 0) == 1 && out[0] == 'b');
    CHECK(ATTR(changed, 0, "x", changed, 4, 0) == -1 && changed[0] == '<');
    CHECK(xml_query_child(changed, 12289, -1, NULL, 0, 0) == -2);
    CHECK(xml_query_child(changed, strlen(changed), -2, NULL, 0, 0) == -2);
    CHECK(xml_query_child(changed, strlen(changed), -1, NULL, 1, 0) == -2);
    CHECK(xml_query_child(changed, strlen(changed), -1, "x", -1, 0) == -2);
    char deep[256] = "";
    for (int i = 0; i < 16; i++)
        strcat(deep, "<r>");
    for (int i = 0; i < 16; i++)
        strcat(deep, "</r>");
    CHECK(CHILD(deep, -1, "r", 0) == 0);
    memcpy(deep + 48, "<r/>", 4);
    CHECK(CHILD(deep, -1, "r", 0) == -2);
    char many[1100] = "<r>";
    for (int i = 0; i < 255; i++)
        strcat(many, "<a/>");
    strcat(many, "</r>");
    CHECK(CHILD(many, 0, "a", 255) == 255);
    memcpy(many + 1023, "<a/></r>", 9);
    CHECK(CHILD(many, 0, "a", 255) == -2);
    REPORT();
}
