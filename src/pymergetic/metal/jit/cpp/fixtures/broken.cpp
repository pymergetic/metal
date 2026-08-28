/* cppx broken fixture: syntactically invalid — the parser must fail with
 * "cppx: ... at line N", never a silent misparse. Line 5: missing ';'. */
int broken() {
    int x = 1
    return x;
}
