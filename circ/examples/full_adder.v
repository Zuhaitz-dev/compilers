module full_adder(input A, B, Cin, output S, Cout);
  wire T1, T2, T3;
  assign T1 = A ^ B;
  assign T2 = T1 & Cin;
  assign T3 = A & B;
  assign S = T1 ^ Cin;
  assign Cout = T2 | T3;
endmodule
