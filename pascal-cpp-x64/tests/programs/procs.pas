program Procs;
var
    g : integer;

procedure greet;
begin
    writeln('Hello, world');
end;

procedure report(score : integer);
begin
    writeln('score = ', score);
end;

procedure add_one(var x : integer);
begin
    x := x + 1;
end;

procedure compute(a : integer; var b : integer);
begin
    b := a * 2;
end;

begin
    g := 5;
    add_one(g);
    writeln(g);
    compute(g, g);
    writeln(g);
    greet;
    report(g);
end.
