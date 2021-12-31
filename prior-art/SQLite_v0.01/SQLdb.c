/*
 *	An SQLdb object gives access to a database. You still need user and
 *	password access to a database and rights to its tables. It is up to the
 *	administrator to provide the necessary user privledges. The PDexternal
 *	provides nothing on top of that for security.
 *
 *	Incoming messages on inlets:
 *		[connect <host> <user> <pass> <dbname> <encoding>(			)]
 *		[disconnect(												)]
 *		[open <filename>(											)] <- for standalone dbs
 *		[close(														)]
 *
 *	Message from SQLquery instances
 *		[query <char *-gpointer_to_sql_buffer> <client_id> <tagged>(					)]
 *			this submits a buffer for evaluation, <tagged> is optional
 *		[submit <char *-gpointer_to_sql_buffer> <client_id> <tagged>(					)]
 *			used with placeholder to compile a query, use 'place' to insert values,
 *			<tagged> is optional
 *		[replace <client_id> ... <tagged>(												)]
 *			same as query, except expects a list containing each of the placeholders,
 *			<tagged> is optional
 *			previously compiled with 'submit'
 *		[next <client_id> <tagged>(														)]
 *			this requests the next result set
 *
 *		These messages are built by a client, and dispatched to THIS object instance.
 *		The <gpointer> is actually a (char *) while points to the c-string buffer for the SQL.
 *
 *	Messages back to SQLquery instances
 *		[result ...(																	)]
 *			sends the next set of results, or an empty list on end
 *		[rowid <float>(																	)]
 *			sends the rowid of the last insert to the client
 *		[error ...(																		)]
 *			sends a status message to the client
 */

/*
 * the base implementation provides SQL services from sqlite3
 */
#include <stdio.h>
#include <string.h>
#include "m_pd.h"
#include "sqlite3.h"

static t_class *SQLdb_class;

typedef struct _SQLdb_connection {
	t_symbol *host;
	t_symbol *user;
	t_symbol *password;
	t_symbol *db_name;
	t_symbol *encoding;
} t_SQLdb_connection;

typedef enum {
	standalone,
	server
} db_types;

typedef struct _SQLdb {
	t_object x_obj;
	
	// this is the name that identifies this object to PD
	t_symbol *id;
	
	// pointers to the database itself
	sqlite3 *db;
	sqlite3_stmt *db_stmt;
	
	db_types db_type;
	union {
		t_SQLdb_connection *conn;
		t_symbol *filename;
	} db_src;
	
	// points to the sql buffer
	char *sql_buffer;
	
	// this is for building the message to the client, in response to its message/request
	int results_count;
	t_atom results[100];
	
	t_atom rowid[1];
	
	// this only ever goes out the SQLdb outlet
	int err_count;
	t_atom err_list[25];
} t_SQLdb;

static int SQLdb_err_to_argv(t_SQLdb *x, t_atom *argv, const char *str) {
	int i;
	int len = strlen(str);
	int j = 0;
	int count = 0;
	char buf[128];
	for(i = 0; i <= len; i++) {
		if (str[i] == ' ' || str[i] == '\0') {
			buf[j] = '\0';
			SETSYMBOL(&argv[count], gensym(buf));
			count++;
			j = 0;
		} else {
			buf[j++] = str[i];
		}
	}
	return count;
}

static void SQLdb_err_into_list(t_SQLdb *x, const char *str) {
	int i;
	int len = strlen(str);
	int j = 0;
	int err_count = 0;
	char buf[128];
	
	for(i = 0; i <= len; i++) {
		if (str[i] == ' ' || str[i] == '\0') {
			buf[j] = '\0';
			SETSYMBOL(&x -> err_list[err_count], gensym(buf));
			err_count++;
			j = 0;
		} else {
			buf[j++] = str[i];
		}
	}
	x -> err_count = err_count;
}

/*
 *	USER METHODS - should be called from the left inlet
 *
 *		SQLdb_connect
 *		SQLdb_disconnect
 *		SQLdb_open
 *		SQLdb_close
 */

/*
 *	[connect <host> <user> <password> <db_name> <encoding>(				)]
 *	 |
 *	[SQLdb me]
 *	 |
 *	[<message display>(							)] <- should read 'connect' upon successful connect
 *														or whatever the database returns
 */
//static void SQLdb_connect(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
//	post("SQLdb_connect: has no meaning in a standalone database");
//}

/*
 *	[disconnect(								)]
 *	 |
 *	[SQLdb me]
 *	 |
 *	[<message display>(							)]
 */
//static void SQLdb_disconnect(t_SQLdb *x) {
//	post("SQLdb_disconnect: has no meaning in a standalone database");
//}

/*
 * open is called from the first inlet
 *
 *	[bang( )]	[bang( )]
 *	 |			 |
 *	[openpanel]	[savepanel]
 *	 |			 /
 *	[open $1(									)]
 *	 |
 *	[SQLdb <name>]
 *	 |
 *	[SQLdb-helper]
 *	 |
 *	[<message display>(							)] <- should read 'open' upon successful open
 *														or whatever the database returns
 */
static void SQLdb_open(t_SQLdb *x, t_symbol *filename) {
	int rc;
	
	post("SQLdb_open: '%s'", filename -> s_name);
	if (x -> db) {
		post("   database already open.");
		post("   use 'close' first.");
		return;
	}
	x -> db_src.filename = filename;
	rc = sqlite3_open(x -> db_src.filename -> s_name, &x -> db);
	if (rc != SQLITE_OK) {
		const char *err = sqlite3_errmsg(x -> db);
		SQLdb_err_into_list(x, err);
		outlet_list(x -> x_obj.ob_outlet,
					&s_list,
					x -> err_count,
					&x -> err_list[0]);
		sqlite3_close(x -> db);
		x -> db = NULL;
		x -> db_src.filename = NULL;
	} else {
		outlet_symbol(x -> x_obj.ob_outlet, gensym("open"));
	}
}

/*
 * this needs to do more than just close the database. it must make sure
 * that all defined statements have been finalized and destroyed. or else
 * it might corrupt the database.
 *
 *	[close(										)]
 *	 |
 *	[SQLdb <name>]
 *	 |
 *	[SQLdb-helper]
 *	 |
 *	[<message display>(							)] <- should read 'close' upon successful close
 */
static void SQLdb_close(t_SQLdb *x) {
	post("SQLdb_close");
	if (x -> db_stmt) {
		sqlite3_finalize(x -> db_stmt);
		x -> db_stmt = NULL;
	}
	if (x -> db) {
		sqlite3_close(x -> db);
		x -> db = NULL;
		outlet_symbol(x -> x_obj.ob_outlet, gensym("close"));
	}
}

static void SQLdb_lastid(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
	t_symbol *client_id = atom_getsymbolarg(0, argc, argv);
	t_float rid;
	
	// get the last row id from the database
	rid = (int) sqlite3_last_insert_rowid(x -> db);
	SETFLOAT(&x -> rowid[0], rid);
	
	// send it back to the client
	pd_typedmess(client_id -> s_thing, gensym("rowid"), 1, &x -> rowid[0]);
}

static void SQLdb_next(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
	// this will return the result set back to the client
	if (x -> db_stmt) {
		int rc = sqlite3_step(x -> db_stmt);
		t_symbol *client_id = atom_getsymbolarg(0, argc, argv);
		t_symbol *tagged_s = atom_getsymbolarg((argc - 1), argc, argv);
		int tagged = (strcmp(tagged_s -> s_name, "tagged") == 0) ? 1 : 0;
	
		post("SQLdb_next");
		if (rc == SQLITE_DONE) {
			sqlite3_finalize(x -> db_stmt);
			x -> db_stmt = NULL;
			// send an empty list back to the client (just contains 'result')
			// this indicates the end of the result set
			pd_typedmess(client_id -> s_thing, gensym("results"), 0, 0);
		} else if (rc == SQLITE_ROW) {
			int col_count, i, col_type;
			t_float col_float;
			
			col_count = sqlite3_column_count(x -> db_stmt);
			x -> results_count = 0;
			for(i = 0; i < col_count; i++) {
				col_type = sqlite3_column_type(x -> db_stmt, i);
				if (tagged) {
					char *col_name = (char *) sqlite3_column_name(x -> db_stmt, i);
					SETSYMBOL(&x -> results[x -> results_count], gensym(col_name));
					x -> results_count++;
				}
				
				switch(col_type) {
					case SQLITE_INTEGER:
						col_float = (t_float) (int) sqlite3_column_double(x -> db_stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_FLOAT:
						col_float = (t_float) sqlite3_column_double(x -> db_stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_TEXT:
						{
							char *col_data = (char *) sqlite3_column_text(x ->db_stmt, i);
							SETSYMBOL(&x -> results[x -> results_count], gensym(col_data));
							x -> results_count++;
						}
						break;
					case SQLITE_BLOB:
					case SQLITE_NULL:
						SETSYMBOL(&x -> results[x -> results_count], gensym("<NIL>"));
						x -> results_count++;
						break;
					default:
						post("not sure what to do with this data type.");
						post("name:'%s' type:%d", (char *)sqlite3_column_name(x -> db_stmt, i), col_type);
				}
			}
			// do output back to the client
			pd_typedmess(client_id -> s_thing, gensym("results"), x -> results_count, &x -> results[0]);
			
		} else if (rc != SQLITE_OK) {
			const char *err = sqlite3_errmsg(x -> db);
			int count = SQLdb_err_to_argv(x, &x -> results[0], err);
			pd_typedmess(client_id -> s_thing, gensym("error"), count, &x -> results[0]);
		}
	} else {
		post("SQLdb_next: No Statement active.");
	}
}

/*
 *	this is sent from the client to make a query into the database. this
 *	does not allow any placeholders to be used, as this expects the SQL
 *	to be entered via the cold inlet on the SQLquery object.
 *
 *	"query" <client_id> <gpointer_to_sql_buffer> "tagged"
 *
 *	s = gensym("query");
 *	<gpointer_to_sql_buffer> needs to be cast back to a (char *)
 *	<client_id> is the bound symbol back to the client
 *	"tagged", optional, indicates field names are included with the result sets
 */
static void SQLdb_query(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
	// do the query stuff
	const char *tail;
	int rc;
	// don't know if this is a big no-no, but here we are
	// we cast our gpointer to a char *, because that is what it is
	char *buff = (char *) argv[0].a_w.w_gpointer;
	t_symbol *client_id = atom_getsymbolarg(1, argc, argv);
	
	post("SQLdb_query");
	x -> sql_buffer = buff;
	fprintf(stderr, "from client:\n%s\n", x -> sql_buffer);
	
	if (x -> db_stmt) {
		post("Hey, bugger, we need to finalize statements.");
		sqlite3_finalize(x -> db_stmt);
	}
	
	rc = sqlite3_prepare_v2(x -> db,
							x -> sql_buffer,
							-1,
							&x -> db_stmt,
							&tail);
	if (rc != SQLITE_OK) {
		const char *err = sqlite3_errmsg(x -> db);
		// yes, we assume that the error buffer is big enough (results that is...)
		int count = SQLdb_err_to_argv(x, &x -> results[0], err);
		fprintf(stderr, "got an error: '%s'\n", err);
		pd_typedmess(client_id -> s_thing, gensym("error"), count, &x -> results[0]);
		return;
	}
	SQLdb_next(x, gensym("next"), argc - 1, &argv[1]);
}

static void SQLdb_replace(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
	post("SQLdb_replace: NOT IMPLEMENTED");
}

static void SQLdb_submit(t_SQLdb *x, t_symbol *s, int argc, t_atom *argv) {
	post("SQLdb_submit: NOT IMPLEMENTED");
}

static void *SQLdb_new(t_symbol *s, int argc, t_atom *argv) {
	t_SQLdb *x = (t_SQLdb *)pd_new(SQLdb_class);
	if (x) {
		// name of this PD instance
		if (argc > 0) {
			int i;
			char buf[100];
			x -> id = atom_getsymbolarg(0, argc, argv);
			pd_bind(&x -> x_obj.ob_pd, x -> id);
			
			for(i = 0; i < argc; i++) {
				atom_string(&argv[i], buf, 100);
				post("[%d]: '%s'", i, buf);
			}
		}
		
		// as this is an sqlite3 database, we are standalone
		x -> db_type = standalone;
		x -> db_src.filename = NULL;
		x -> db = NULL;
		x -> db_stmt = NULL;
		
		x -> sql_buffer = NULL;
		
		x -> results_count = 0;
		x -> err_count = 0;
		
		outlet_new(&x -> x_obj, NULL);
	}
	post("SQLdb_new: '%ld'", (long int)x);
	return x;
}

static void SQLdb_free(t_SQLdb *x) {
	SQLdb_close(x);
	pd_unbind(&x -> x_obj.ob_pd, x -> id);
}

void SQLdb_setup(void) {
	post("SQLdb_setup");
	
	SQLdb_class = class_new(gensym("SQLdb"),
							 (t_newmethod)SQLdb_new,
							 (t_method)SQLdb_free,
							 sizeof(t_SQLdb),
							 0,
							 A_GIMME,
							 0);
	
//	class_addlist(SQLdb_class, SQLdb_list);
//	class_addanything(SQLdb_class, SQLdb_anything);
	
	class_addmethod(SQLdb_class, (t_method)SQLdb_query, gensym("query"), A_GIMME, 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_replace, gensym("place"), A_GIMME, 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_submit, gensym("compile"), A_GIMME, 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_next, gensym("next"), A_GIMME, 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_lastid, gensym("lastid"), A_GIMME, 0);
	
//	class_addmethod(SQLdb_class, (t_method)SQLdb_connect, gensym("connect"), A_GIMME, 0);
//	class_addmethod(SQLdb_class, (t_method)SQLdb_disconnect, gensym("disconnect"), 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_open, gensym("open"), A_SYMBOL, 0);
	class_addmethod(SQLdb_class, (t_method)SQLdb_close, gensym("close"), 0);
}
