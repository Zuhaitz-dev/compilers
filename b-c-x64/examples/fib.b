print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

fib(n) {
    if (n < 2) return (n);
    return (fib(n - 1) + fib(n - 2));
}

main() {
    extrn b_print;
    auto i;
    i = 0;
    while (i <= 10) {
        print_num(fib(i));
        b_print(" ");
        i++;
    }
    b_print("*n");
}
