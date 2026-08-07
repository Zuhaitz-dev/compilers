/* Bubble sort in B. */

print_num(n) {
    extrn b_putchar;
    auto a;
    if (a = n / 10) print_num(a);
    b_putchar(n % 10 + '0');
}

arr[10];

main() {
    extrn b_print;
    auto i, j, tmp, swapped;
    arr[0] = 42;
    arr[1] = 17;
    arr[2] = 9;
    arr[3] = 33;
    arr[4] = 5;
    arr[5] = 77;
    arr[6] = 2;
    arr[7] = 21;
    arr[8] = 12;
    arr[9] = 60;

    i = 0;
    while (i < 10) {
        j = 0;
        swapped = 0;
        while (j < 10 - i - 1) {
            if (arr[j] > arr[j + 1]) {
                tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
                swapped = 1;
            }
            j++;
        }
        if (!swapped) break;
        i++;
    }

    i = 0;
    while (i < 10) {
        print_num(arr[i]);
        b_print(" ");
        i++;
    }
    b_print("*n");
}
