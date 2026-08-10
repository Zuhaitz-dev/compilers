program Arith;
var
    a, b, c : integer;
    r : real;
begin
    a := 10;
    b := 3;
    c := a + b * 2;
    writeln(c);
    writeln(a div b);
    writeln(a mod b);
    r := a / b;
    writeln(r);
    writeln(-a);
end.
