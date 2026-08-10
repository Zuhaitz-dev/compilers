/* Sieve of Eratosthenes in B. */

LIMIT = 100;
sieve[101];

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

main() {
    extrn b_print;
    auto i, j;
    i = 2;
    while (i <= LIMIT) {
        sieve[i] = 1;
        i++;
    }
    i = 2;
    while (i * i <= LIMIT) {
        if (sieve[i]) {
            j = i * i;
            while (j <= LIMIT) {
                sieve[j] = 0;
                j = j + i;
            }
        }
        i++;
    }
    i = 2;
    while (i <= LIMIT) {
        if (sieve[i]) {
            print_num(i);
            b_print(" ");
        }
        i++;
    }
    b_print("*n");
}
