program Conditionals;
var
    x : integer;
    b : boolean;
begin
    x := 7;
    if x > 5 then
        writeln('big')
    else
        writeln('small');

    if (x mod 2) = 0 then
        writeln('even')
    else
        writeln('odd');

    b := (x >= 7) and (x < 10);
    if b then
        writeln('in-range');

    if not (x = 7) then
        writeln('not-seven')
    else
        writeln('seven');
end.
