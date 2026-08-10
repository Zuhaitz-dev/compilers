program Funcs;
var
    n : integer;

function fact(n : integer) : integer;
begin
    if n <= 1 then
        fact := 1
    else
        fact := n * fact(n - 1);
end;

function fib(n : integer) : integer;
begin
    if n < 2 then
        fib := n
    else
        fib := fib(n - 1) + fib(n - 2);
end;

function max2(a : integer; b : integer) : integer;
begin
    if a > b then
        max2 := a
    else
        max2 := b;
end;

function half(x : integer) : real;
begin
    half := x / 2;
end;

begin
    n := 5;
    writeln(fact(n));
    for n := 0 to 8 do
        writeln(fib(n));
    writeln(max2(3, 9));
    writeln(half(7));
end.
