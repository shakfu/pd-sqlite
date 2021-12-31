# pd-sqlite

This project captures my research and efforts to develop a minimal sqlite3 external object for pure-data.

## Objective

The idea is to have a very simple (1 inlet, 1 outlet) sqlite3 wrapper to support sql input via messages to the inlet and puredata friendly data (or atoms) out of the outlet.

## Current State

The current implemented flow which was 'borrowed' from Iain Duncan's Scheme-for-PD converts a list to a symbol as follows:

```
[sql msg] -> [fudiformat -u] -> [list tosymbol] -> [sqlite3 external]
```

This approach works for simple sql statements. However, it falls flat when it comes to comma-separated sql statements such `create table t ( id integer primary key, name text )`.




## Development Notes

- Escaping the comma with backslashes and use of the [text] dont' work either.

- Perhaps string contatenation via something like the following (see also: https://stackoverflow.com/questions/58160663/join-strings-in-c-with-a-separator):

```
char str[80];
strcpy(str, "these ");
strcat(str, "strings ");
strcat(str, "are ");
strcat(str, "concatenated.");
```

- Check how prior art has solved the problem (see `prior-art folder`).