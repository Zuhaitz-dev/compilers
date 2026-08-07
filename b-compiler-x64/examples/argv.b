/* Print the command-line arguments in B. */

main(argc, argv) {
    extrn b_print;
    auto i;
    i = 1;
    while (i < argc) {
        b_print(argv[i]);
        b_print(" ");
        i++;
    }
    b_print("*n");
}
