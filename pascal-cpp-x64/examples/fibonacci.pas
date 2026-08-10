program Fibonacci;
var
    n : integer;

function fib(n : integer) : integer;
begin
    if n < 2 then
        fib := n
    else
        fib := fib(n - 1) + fib(n - 2);
end;

begin
    for n := 0 to 10 do
        writeln(fib(n));
end.
