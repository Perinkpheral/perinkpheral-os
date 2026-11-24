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
    const char bulk[] = "<r name='ЖЖЖЖ' id='ab' empty='' escaped='&amp;'/>";
    host_xml_field_t fields[] = {{"name", 0, 6, HOST_XML_LABEL, {0}},
                                 {"id", 6, 4, HOST_XML_REQUIRED, {0}},
                                 {"absent", 10, 1, 0, {0}},
                                 {"empty", 11, 1, 0, {0}},
                                 {"escaped", 12, 2, 0, {0}}};
    memset(out, 'x', sizeof out);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, sizeof out) == 27);
    CHECK(!strcmp(out, "Ж...") && !strcmp(out + 6, "ab") && !out[10] && !out[11] &&
          !strcmp(out + 12, "&"));
    CHECK(out[14] == 'x');
    char before[sizeof out];
    memcpy(before, out, sizeof out);
    fields[1].capacity = 2;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, sizeof out) == -1);
    CHECK(!memcmp(out, before, sizeof out));
    fields[1].capacity = 4;
    fields[3].flags = HOST_XML_REQUIRED;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, sizeof out) == -1);
    fields[3].flags = 0;
    fields[2].flags = HOST_XML_REQUIRED;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, sizeof out) == -1);
    fields[2].flags = 0;
    fields[3].offset = 10;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, sizeof out) == -1);
    fields[3].offset = 11;
    const char duplicate[] = "<r name='valid' id='ab' id='cd'/>";
    CHECK(xml_query_attrs(duplicate, strlen(duplicate), 0, fields, 5, out, sizeof out) == -1);
    const char invalid[] = "<r name='valid' id='ab' escaped='&no;'/>";
    CHECK(xml_query_attrs(invalid, strlen(invalid), 0, fields, 5, out, sizeof out) == -1);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, (char *)fields, 32) == -1);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0,
                          (const host_xml_field_t *)((const char *)fields + 1), 5, out, 32) == -1);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 17, out, 32) == -1);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 0, out, 32) == -1);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, (char *)bulk, 32) == -1);
    CHECK(xml_query_attrs((char *)fields, 32, 0, fields, 5, out, 32) == -1);
    fields[2].reserved[0] = 1;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, 32) == -1);
    fields[2].reserved[0] = 0;
    fields[2].flags = 4;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, 32) == -1);
    fields[2].flags = 0;
    strcpy(fields[2].name, "id");
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, 32) == -1);
    memset(fields[2].name, 'x', sizeof fields[2].name);
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, 32) == -1);
    fields[1].offset = 65535;
    CHECK(xml_query_attrs(bulk, strlen(bulk), 0, fields, 5, out, 32) == -1);
    CHECK(!memcmp(out, before, sizeof out));
    host_xml_field_t sixteen[16] = {0};
    char all[180] = "<r ";
    for (int i = 0; i < 16; i++) {
        snprintf(sixteen[i].name, sizeof sixteen[i].name, "a%d", i);
        sixteen[i].offset = 2 * i;
        sixteen[i].capacity = 2;
        snprintf(all + strlen(all), sizeof all - strlen(all), "a%d='x' ", i);
    }
    strcat(all, "/>");
    CHECK(xml_query_attrs(all, strlen(all), 0, sixteen, 16, out, 32) == 65535);
    CHECK(!strcmp(out + 30, "x"));
    const char *unrelated[] = {"<r id='good' x='p' x='q'/>", "<r id='good'><s x='&bogus;'/></r>",
                                "<r id='good'><s x='p' x='q'/></r>"};
    const host_xml_field_t id_field = {"id", 0, 8, HOST_XML_REQUIRED, {0}};
    for (unsigned i=0;i<sizeof unrelated/sizeof unrelated[0];i++) {
        const char *doc=unrelated[i];
        strcpy(out,"keep");
        CHECK(!xml_document_valid(doc,strlen(doc)));
        CHECK(CHILD(doc,-1,"r",0)==-2);
        CHECK(ATTR(doc,0,"id",out,8,0)==-1 && !strcmp(out,"keep"));
        CHECK(xml_query_attrs(doc,strlen(doc),0,&id_field,1,out,8)==-1 && !strcmp(out,"keep"));
    }
    REPORT();
}
