program CaseTest;
var
    x : integer;
begin
    x := 3;
    case x of
        1: writeln('one');
        2: writeln('two');
        3, 4: writeln('three-or-four');
    else
        writeln('other');
    end;

    x := 9;
    case x of
        1: writeln('one');
        2: writeln('two');
        3, 4: writeln('three-or-four');
    else
        writeln('other');
    end;
end.
