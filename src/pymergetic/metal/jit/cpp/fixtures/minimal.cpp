/* cppx minimal fixture: plain functions, locals, control flow, expressions.
 * The full syntactic layer the parser must handle; no templates, no classes. */
int add(int a, int b) {
    return a + b;
}

int main() {
    int total = 0;
    for (int i = 0; i < 10; i++) {
        total = total + i;
    }
    if (total > 5) {
        return add(total, 1);
    }
    while (total < 100) {
        total += 10;
    }
    return total;
}
