program Arrays;
var
    a : array[1..5] of integer;
    chars : array[0..3] of char;
    i : integer;
    total : integer;
begin
    for i := 1 to 5 do
        a[i] := i * i;
    for i := 1 to 5 do
        writeln(a[i]);

    total := 0;
    for i := 1 to 5 do
        total := total + a[i];
    writeln(total);

    chars[0] := 'h';
    chars[1] := 'i';
    chars[2] := '!';
    chars[3] := '?';
    for i := 0 to 3 do
        writeln(chars[i]);
end.
