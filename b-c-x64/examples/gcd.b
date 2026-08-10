/* Greatest common divisor (Euclidean algorithm) in B. */

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

gcd(a, b) {
    auto r;
    while (b) {
        r = a % b;
        a = b;
        b = r;
    }
    return (a);
}

main() {
    extrn b_print;
    print_num(gcd(48, 36));
    b_print("*n");
    print_num(gcd(17, 5));
    b_print("*n");
    print_num(gcd(252, 105));
    b_print("*n");
    print_num(gcd(13, 39));
    b_print("*n");
}
