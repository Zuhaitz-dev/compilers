print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

fact(n) {
    if (n <= 1) return (1);
    return (n * fact(n - 1));
}

main() {
    extrn b_print;
    auto i;
    i = 1;
    while (i <= 10) {
        b_print("fact(");
        print_num(i);
        b_print(") = ");
        print_num(fact(i));
        b_print("*n");
        i++;
    }
}
