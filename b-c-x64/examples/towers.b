/* Towers of Hanoi in B. */

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

hanoi(n, from, to, via) {
    extrn b_print, b_putchar;
    if (n == 1) {
        b_print("Move disk 1 from ");
        b_putchar(from);
        b_print(" to ");
        b_putchar(to);
        b_print("*n");
        return (0);
    }
    hanoi(n - 1, from, via, to);
    b_print("Move disk ");
    print_num(n);
    b_print(" from ");
    b_putchar(from);
    b_print(" to ");
    b_putchar(to);
    b_print("*n");
    hanoi(n - 1, via, to, from);
}

main() {
    extrn b_print;
    b_print("Towers of Hanoi, 3 disks:*n");
    hanoi(3, 'A', 'C', 'B');
}
