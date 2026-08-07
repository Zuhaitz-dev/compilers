/* Classic FizzBuzz, B-style. */

N = 30;

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

fizzbuzz(n) {
    extrn b_print;
    if (n % 15 == 0) b_print("FizzBuzz*n");
    if (n % 15 != 0 && n % 3 == 0) b_print("Fizz*n");
    if (n % 15 != 0 && n % 5 == 0) b_print("Buzz*n");
    if (n % 15 != 0 && n % 3 != 0 && n % 5 != 0) {
        print_num(n);
        b_print("*n");
    }
}

main() {
    auto i;
    for (i = 1; i <= N; i++) fizzbuzz(i);
}
