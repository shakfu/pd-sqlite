# pd-sqlite

This project has two parallel objectives:

1. Store and update Michael J McGonagle's `Sqlite3db` project on Github.
2. Learn from prior and develop a minimal sqlite3 external object for pure-data based on current practices.

## Objective 1: Preserve Prior Art

There's actually a very nicely implemented solution by Michael J McGonagle, which was developed around 2009 and which compiles and works well but which is not on github but on http://puredata.info/Members/mjmogo .

I have copied and placed these solutions untouched in the prior art folder for posterity and I would like to update the most mature version, `sqlite3db`, and possibly make it simpler and more friendly to use. A copy of `sqlite3db` called `sql3db` in the project directory will be used for this purpose.

- [x] prefer static compilation with`libsqlite3.a` instead of dynamic linking
- [x] update build system to use `pd-lib-builder`
- [ ] add currentdir() to make db opening work as expected.


## Objective 2: Minimal Sqlite External

The idea is to have, if possible, a very simple (1 inlet, 1 outlet) sqlite3 wrapper to support sql input via messages to the inlet and puredata friendly data (or atoms) out of the outlet.

This work is currently being developed in the `sql3.c` object.

- [x] fix text input / comma escape problem (partial hackish fix)
- [x] create table
- [x] insert into
- [ ] select 
- [ ] experiment with cpp wrapper [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp)

### Current State

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
