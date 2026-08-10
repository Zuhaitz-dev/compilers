program Loops;
var
    i, sum : integer;
begin
    sum := 0;
    for i := 1 to 10 do
        sum := sum + i;
    writeln(sum);

    sum := 0;
    for i := 10 downto 1 do
        sum := sum + i;
    writeln(sum);

    i := 0;
    while i < 5 do
    begin
        i := i + 1;
        writeln(i);
    end;

    i := 0;
    repeat
        i := i + 2;
    until i >= 6;
    writeln(i);
end.
