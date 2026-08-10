/* Collatz sequence (3n+1 problem) in B. */

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

main() {
    extrn b_print;
    auto n, steps;
    n = 27;
    steps = 0;
    while (n != 1) {
        print_num(n);
        b_print(" ");
        if (n % 2) n = 3 * n + 1;
        else n = n / 2;
        steps++;
    }
    b_print("1*n");
    b_print("steps: ");
    print_num(steps);
    b_print("*n");
}
