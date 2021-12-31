/*
 *	SQLite3 Database Driver
 *
 *	The design goal behind the sqlite3 library is a combination of a [textfile]/[qlist]
 *	object and a [struct]. The [sqlite3db] object is used to create a connection to a
 *	database, and provide a key for use in Pd. The [sqlite3query] object is used to create
 *	prepared statements to access the information in a database.
 *
 *	There are four objects implemented.
 *		[sqlite3db <db-key>]
 *			This object manages a database. This database can either be opened from a file,
 *			created on disk, or created in memory (not directly stored on disk). It will also
 *			accept SQL statements for managing the database using SQL statements. The SQL is
 *			entered by sending messages to the cold (right) inlet of the database object.
 *			
 *			The <db-key> is used within Pd to indentify an instance of a database.
 *
 *		[sqlite3query <sql statement>]
 *			This object would be analogous to either a 'get' or a 'set' object when using
 *			structs. The SQL can be entered at creation time, or created dynamically, prepared,
 *			and executed by sending the SQL 
 *
 *		[sqlite3rowid]
 *			This object is used to get the Row ID of the last row inserted into a database. It
 *			is used when you need to store a Foreign Key in another table to reference this
 *			row of data.
 *
 *		[sqlite3blob <encoding>]
 *			Currently, this is not implemented, but the idea for handling a BLOB SQL datatype is
 *			to provide an iterator that can store or retreive the data, based on the encoding of
 *			the data. While it might be desirable to treat a BLOB as a string (or Pd symbol), Pd
 *			keeps all symbols in memory, whether they are still being used or not. (maybe the best
 *			idea would be to create a new Pd pointer type)
 *
 *	There are a couple of issues when dealing with an SQL statement. There can be no use
 *	of the character '\', as Pd does not allow them.
 */
#include <stdio.h>
#include <string.h>
#include "m_pd.h"
#include "sqlite3.h"

//#define DEBUG(x) x
#define DEBUG(x)

#define sFALSE 0
#define sTRUE ~sFALSE

#define DATABASE_COUNT 10
#define STATEMENT_COUNT 100
#define RESULTS_COUNT 100
#define ERRORS_COUNT 25
#define BLOB_COUNT 50

static t_atom comma_atom;

static t_class *collector_class;
static t_class *collector_cold_class;

static t_class *sqlite3db_class;
static t_class *sqlite3db_cold_class;

static t_class *sqlite3query_class;
static t_class *sqlite3query_cold_class;

static t_class *sqlite3rowid_class;

static t_class *sqlite3blob_class;

typedef struct _collector_cold {
	t_pd	c_pd;
	void	*collector;
} t_collector_cold;

typedef struct _collector {
	t_object			x_obj;
	
	// these things need to be the first things in 'sqlite3db' and 'sqlite3query'
	t_binbuf			*bbuf;
	int					begin_chunk;
	t_collector_cold	cold;
	
	t_outlet			*status_outlet;
} t_collector;


typedef struct _sqlite3db_cold {
	t_pd	d_pd;
	void	*db;
} t_sqlite3db_cold;

typedef struct _sqlite3db {
	t_object			x_obj;
	
	t_binbuf			*bbuf;
	int					begin_chunk;
	t_sqlite3db_cold	cold;
	char				sql_buffer[MAXPDSTRING];
	
	t_outlet			*status_outlet;
	
	sqlite3				*db;
	t_symbol			*filename;
	t_symbol			*db_key;
	int					ref_count;
	int					sql_processed;
	sqlite3_stmt		*stmt;
	
	int					tagged;
	int					results_count;
	t_atom				results[RESULTS_COUNT];
	
	int					errors_count;
	t_atom				error[ERRORS_COUNT];
} t_sqlite3db;

typedef struct _sqlite3query_cold {
	t_pd	q_pd;
	void	*query;
} t_sqlite3query_cold;

typedef struct _sqlite3query {
    t_object            x_obj;
	
	t_binbuf			*bbuf;
	int					begin_chunk;
	t_sqlite3query_cold	cold;
	char				sql_buffer[MAXPDSTRING];
	
	int					sql_processed;
	sqlite3_stmt		*stmt;
	
	t_sqlite3db			*dbadmin;
	
	t_outlet			*error_outlet;
	t_outlet			*status_outlet;
	
	int					tagged;
	int					results_count;
	t_atom				results[RESULTS_COUNT];
	
	int					errors_count;
	t_atom				error[ERRORS_COUNT];
} t_sqlite3query;

typedef struct _sqlite3rowid {
	t_object			x_obj;
	int					db_index;
} t_sqlite3rowid;

typedef struct _sqlite3blob {
	t_object			x_obj;
	
	int					ref_count;
} t_sqlite3blob;

typedef struct _sqlite3library {
	t_sqlite3db		*db[DATABASE_COUNT];
	t_sqlite3query	*queries[STATEMENT_COUNT];
} t_sqlite3library;
static t_sqlite3library sqlite3library;

static int sqlite3library_lookupdb(t_symbol *db_key) {
	int i;
	for (i = 0; i < DATABASE_COUNT; i++) {
		if (sqlite3library.db[i] && sqlite3library.db[i] -> db_key == db_key) {
			return i;
		}
	}
	return -1;
}

static int sqlite3library_error_to_atoms(t_atom *to, const char *str) {
	int i;
	int len = strlen(str);
	int j = 0;
	int count = 0;
	char buf[MAXPDSTRING];
	for (i = 0; i <= len; i++) {
		if (str[i] == ' ' || str[i] == '\0') {
			buf[j] = '\0';
			SETSYMBOL(&to[count], gensym(buf));
			count++;
			j = 0;
		} else {
			buf[j++] = str[i];
		}
	}
	return count;
}

static void collector_cold_init(t_collector_cold *c, void *collector) {
	DEBUG(post("cold_init"));
	
	c -> c_pd = collector_cold_class;
	c -> collector = collector;
}

static void collector_cold_anything(t_collector_cold *c, t_symbol *s, int argc, t_atom *argv) {
	t_collector *x = (t_collector *)c -> collector;
	t_binbuf *b = x -> bbuf;
	
	DEBUG(post("cold_anything:"));
	if (x -> begin_chunk) {
		binbuf_add(b, 1, &comma_atom);
	} else {
		x -> begin_chunk = sTRUE;
	}
	if (s != &s_float) {
		t_atom sel;
		SETSYMBOL(&sel, s);
		binbuf_add(b, 1, &sel);
	}
	binbuf_add(b, argc, argv);
}

static void collector_clear(t_collector *x) {
	DEBUG(post("collector_clear"));
	binbuf_clear(x -> bbuf);
	x -> begin_chunk = sFALSE;
}

static void collector_bang(t_collector *x) {
	DEBUG(post("collector_bang"));
	outlet_anything(x -> x_obj.ob_outlet, &s_list, binbuf_getnatom(x -> bbuf), binbuf_getvec(x -> bbuf));
}

static void *collector_new(t_symbol *s, int argc, t_atom *argv) {
	t_collector *x = (t_collector *)pd_new(collector_class);
	if (x) {
		x -> bbuf = binbuf_new();
		
		// cold inlet stuff
		collector_cold_init(&x -> cold, x);
		inlet_new(&x -> x_obj, &x -> cold.c_pd, 0, 0);
		
		// outlet stuff
		outlet_new(&x -> x_obj, &s_list);
		x -> status_outlet = outlet_new(&x -> x_obj, 0);
	}
	return x;
}

static void collector_free(t_collector *x) {
	if (x -> bbuf) {
		binbuf_free(x -> bbuf);
	}
}

static void sqlite3db_error(t_sqlite3db *x, const char *err) {
	x -> errors_count = sqlite3library_error_to_atoms(&x -> error[0], err);
	outlet_anything(x -> x_obj.ob_outlet, gensym("error"), x -> errors_count, &x -> error[0]);
}

static void sqlite3db_clear(t_sqlite3db *x) {
	DEBUG(post("sqlite3db_clear"));
	
	strcpy(x -> sql_buffer, "");
	binbuf_clear(x -> bbuf);
	x -> sql_processed = sFALSE;
	x -> begin_chunk = sFALSE;
}

/*
 *	a [bang( will first check to see if there is a valid statement
 *	already active, and if not if there is any sql in the buffer,
 *	it will be sent to sqlite3 for processing
 */
static void sqlite3db_bang(t_sqlite3db *x) {
	DEBUG(post("sqlite3db_bang"));
	
	if (x -> stmt) {
		t_symbol *message = NULL;
		// this means that we have an active statement, so we just step it
		int rc = sqlite3_step(x -> stmt);
		DEBUG(post("sqlite3db_bang: stepping"));
		if (rc == SQLITE_DONE) {
			sqlite3_finalize(x -> stmt);
			x -> stmt = NULL;
			outlet_bang(x -> status_outlet);
			sqlite3db_clear(x);
			return;
		} else if (rc == SQLITE_ROW) {
			int col_count, i, col_type;
			t_float col_float;
			
			col_count = sqlite3_column_count(x -> stmt);
			x -> results_count = 0;
			for (i = 0; i < col_count; i++) {
				col_type = sqlite3_column_type(x -> stmt, i);
				if (x -> tagged) {
					char *col_name = (char *) sqlite3_column_name(x -> stmt, i);
					SETSYMBOL(&x -> results[x -> results_count], gensym(col_name));
					x -> results_count++;
				}
				switch(col_type) {
					case SQLITE_INTEGER:
						col_float = (t_float) (int) sqlite3_column_double(x -> stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_FLOAT:
						col_float = (t_float) sqlite3_column_double(x -> stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_TEXT:
						{
							char *col_data = (char *) sqlite3_column_text(x ->stmt, i);
							SETSYMBOL(&x -> results[x -> results_count], gensym(col_data));
							x -> results_count++;
						}
						break;
					case SQLITE_BLOB:
					case SQLITE_NULL:
						SETSYMBOL(&x -> results[x -> results_count], gensym("<NULL>"));
						x -> results_count++;
						break;
					default:
						post("not sure what to do with this data type.");
						post("name:'%s' type:%d", (char *)sqlite3_column_name(x -> stmt, i), col_type);
				}
			}
			message = gensym("results");
		} else if (rc != SQLITE_OK) {
			sqlite3db_error(x, sqlite3_errmsg(x -> db));
			return;
		}
		outlet_anything(x -> x_obj.ob_outlet, message, x -> results_count, &x -> results[0]);
//	} else if (x -> db && strlen(x -> sql_buffer) > 0) {
	} else if (x -> db && binbuf_getnatom(x -> bbuf) > 0) {
		// we have some awaiting sql to process
		const char *tail;
		char *buf_text;
		int i, length;
		
		binbuf_gettext(x -> bbuf, &buf_text, &length);
		// while binbuf appears to use 'strcpy', it may not return a null terminated string
		for(i = 0; i < length; i++) {
			x -> sql_buffer[i] = buf_text[i];
		}
		x -> sql_buffer[length] = '\0';
		// also, I think we need to free the string returned by 'binbuf_gettext'
		freebytes(buf_text, length);
		
		fprintf(stderr, "sql: '%s'\n", x -> sql_buffer);
		
		int rc = sqlite3_prepare_v2(x -> db, x -> sql_buffer, -1, &x -> stmt, &tail);
		if (rc != SQLITE_OK) {
			sqlite3db_error(x, sqlite3_errmsg(x -> db));
			return;
		}
		x -> sql_processed = sTRUE;
		sqlite3db_bang(x);
//	} else {
//		const char *err = "doing nothing";
//		x -> errors_count = sqlite3library_error_to_atoms(&x -> error[0], err);
//		outlet_anything(x -> x_obj.ob_outlet, gensym("error"), x -> errors_count, &x -> error[0]);
	}
}

static void sqlite3db_open(t_sqlite3db *x, t_symbol *filename) {
	DEBUG(post("sqlite3db_open: '%s'", filename -> s_name));
	if (x -> db_key) {
		if (x -> db == NULL) {
			// need to open the database
			int rc = sqlite3_open(filename -> s_name, &x -> db);
			if (rc == SQLITE_OK) {
				x -> filename = filename;
				
				SETSYMBOL(&x -> results[0], filename);
				x -> results_count = 1;
				outlet_anything(x -> x_obj.ob_outlet, gensym("open"), x -> results_count, &x -> results[0]);
			} else {
				sqlite3db_error(x, sqlite3_errmsg(x -> db));
				sqlite3_close(x -> db);
				x -> db = NULL;
			}
		} else {
			sqlite3db_error(x, "database already open");
		}
	}
}

static void sqlite3query_finalize(t_sqlite3query *x) {
	DEBUG(post("sqlite3query_finalize"));
	if (x -> stmt) {
		sqlite3_finalize(x -> stmt);
		x -> stmt = NULL;
		x -> dbadmin -> ref_count--;
	}
}

static void sqlite3db_close(t_sqlite3db *x) {
	DEBUG(post("sqlite3db_close"));
	if (x -> db) {
		int i;
		// first check to make sure all statements are finalized
		for (i = 0; i < STATEMENT_COUNT; i++) {
			if (sqlite3library.queries[i] && sqlite3library.queries[i] -> dbadmin == x) {
				sqlite3query_finalize(sqlite3library.queries[i]);
			}
		}
		sqlite3_close(x -> db);
		x -> db = NULL;
		x -> filename = NULL;
		
		// also, make sure that the sql_buffer is empty
		sqlite3db_clear(x);
		
		outlet_anything(x -> x_obj.ob_outlet, gensym("close"), 0, 0);
	}
}

static void sqlite3db_report(t_sqlite3db *x) {
	int i;
	
	post("sqlite3db_report");
	post(" x -> db: %s", (x -> db) ? "open" : "closed");
	post(" x -> db_key: %s", (x -> db_key) ? x -> db_key -> s_name : "<NULL>");
	post(" x -> filename: %s", (x -> filename) ? x -> filename -> s_name : "<NULL>");
	post(" x -> ref_count: %d", x -> ref_count);
	post(" x -> sql_buffer: '%s'", x -> sql_buffer);
	post(" x -> sql_processed: %s", (x -> sql_processed) ? "true" : "false");
	post(" x -> stmt: %s", (x -> stmt) ? "defined" : "undefined");
	post("statements:");
	
	for (i = 0; i < STATEMENT_COUNT; i++) {
		if (sqlite3library.queries[i] && sqlite3library.queries[i] -> dbadmin == x) {
			post("[%d]: %s", i, (sqlite3library.queries[i]) ? "exists" : "NULL");
			post(" stmt[%d]: %s", i, (sqlite3library.queries[i] -> stmt) ? "prepared" : "NULL");
			if (sqlite3library.queries[i] -> stmt) {
				int c = sqlite3_bind_parameter_count(sqlite3library.queries[i] -> stmt);
				post(" stmt[%d] expects %d parameter%s", i, c, (c != 1) ? "s" : "");
			}
			post(" stmt[%d] -> sql_buffer: '%s'", i, sqlite3library.queries[i] -> sql_buffer);
		}
	}
}

static void sqlite3db_tagged(t_sqlite3db *x, t_floatarg f) {
	DEBUG(post("sqlite3query_tagged: %g", f));
	x -> tagged = (f == sFALSE) ? sFALSE : sTRUE;
}

static void sqlite3db_rowid(t_sqlite3db *x) {
	if (x -> db) {
		t_atom row_id;
		SETFLOAT(&row_id, (t_float) (int) sqlite3_last_insert_rowid(x -> db));
		outlet_anything(x -> x_obj.ob_outlet, gensym("rowid"), 1, &row_id);
	}
}

static void sqlite3db_cold_init(t_sqlite3db_cold *p, t_sqlite3db *db) {
	p -> d_pd = sqlite3db_cold_class;
	p -> db = (void *)db;
}

static void sqlite3db_cold_anything(t_sqlite3db_cold *p, t_symbol *s, int argc, t_atom *argv) {
//	int i;
//	char buf[MAXPDSTRING];
	t_sqlite3db *x = (t_sqlite3db *)p -> db;
//	char *b = x -> sql_buffer;
	
	// post("sqlite3db_cold_anything: '%s'", s -> s_name);
	if (x -> sql_processed) {
		//post("sql_processed");
		if (x -> stmt) {
			sqlite3_finalize(x -> stmt);
			x -> stmt = NULL;
		}
		sqlite3db_clear(x);
	}
	collector_cold_anything((t_collector_cold *)p, s, argc, argv);
//	if (x -> begin_chunk) {
//		strcat(b, ", ");
//	} else {
//		x -> begin_chunk = sTRUE;
//	}
//	if (s != &s_float) {
//		strcat(b, s -> s_name);
//		strcat(b, " ");
//	}
//	for (i = 0; i < argc; i++) {
//		atom_string(&argv[i], buf, MAXPDSTRING);
//		strcat(b, buf);
//		if (i < (argc - 1)) {
//			strcat(b, " ");
//		}
//	}
}

/*
 *	[sqlite3db <db_key>]
 */
static void *sqlite3db_new(t_symbol *s, int argc, t_atom *argv) {
	t_sqlite3db *x = (t_sqlite3db *)pd_new(sqlite3db_class);
	if (x) {
		if (argc > 0) {
			int i;
			
			// set the key for this instance
			x -> db_key = atom_getsymbol(&argv[0]);
			
			// does key already exist
			for (i = 0; i < DATABASE_COUNT; i++) {
				if (sqlite3library.db[i]) {
					if (x -> db_key == sqlite3library.db[i] -> db_key) {
						post("Duplicate db_key '%s'", x -> db_key -> s_name);
						pd_free((void *)x);
						return NULL;
					}
				} else {
					sqlite3library.db[i] = x;
					break;
				}
			}
			if (i == DATABASE_COUNT) {
				// too many databases defined
				post("Too many databases defined. Increase DATABASE_COUNT (%d) and recompile", DATABASE_COUNT);
				pd_free((void *)x);
				return NULL;
			}
			x -> db = NULL;
			x -> filename = NULL;
			x -> ref_count = 0;
			x -> bbuf = binbuf_new();
			strcpy(x -> sql_buffer, "");
			x -> sql_processed = sFALSE;
			x -> begin_chunk = sFALSE;
			x -> stmt = NULL;
			x -> tagged = sFALSE;
			x -> results_count = 0;
			
			// set up inlets
			sqlite3db_cold_init(&x -> cold, x);
			inlet_new(&x -> x_obj, &x -> cold.d_pd, 0, 0);
			
			// set up outlets
			outlet_new(&x -> x_obj, &s_list);
			x -> status_outlet = outlet_new(&x -> x_obj, 0);
		}
	} else {
		x -> db_key = NULL;
		
		x -> db = NULL;
		x -> filename = NULL;
		x -> ref_count = 0;
		x -> bbuf = binbuf_new();
		strcpy(x -> sql_buffer, "");
		x -> sql_processed = sFALSE;
		x -> begin_chunk = sFALSE;
		x -> stmt = NULL;
		x -> tagged = sFALSE;
		x -> results_count = 0;
	}
	return x;
}

static void sqlite3db_free(t_sqlite3db *x) {
	int i;
	// post("sqlite3db_free");
	
	// is the statement still acive
	if (x -> stmt) {
		sqlite3_finalize(x -> stmt);
	}
	if (x -> db) {
		// check to make sure that no other statements attached to this database are active
		if (x -> ref_count > 0) {
			for (i = 0; i < STATEMENT_COUNT; i++) {
				if (sqlite3library.queries[i] && sqlite3library.queries[i] -> stmt) {
					sqlite3query_finalize(sqlite3library.queries[i]);
					sqlite3library.queries[i] -> dbadmin = NULL;
				}
			}
		}
		sqlite3_close(x -> db);
	}
	for (i = 0; i < DATABASE_COUNT; i++) {
		if (sqlite3library.db[i] == x) {
			sqlite3library.db[i] = NULL;
			break;
		}
	}
}

static void sqlite3query_error(t_sqlite3query *x, const char *err) {
	x -> errors_count = sqlite3library_error_to_atoms(&x -> error[0], err);
	outlet_anything(x -> error_outlet, gensym("error"), x -> errors_count, &x -> error[0]);
}

static void sqlite3query_bang(t_sqlite3query *x) {
	DEBUG(post("sqlite3query_bang"));
	if (x -> stmt) {
		t_symbol *message = NULL;
		int rc = sqlite3_step(x -> stmt);
		if (rc == SQLITE_DONE) {
			sqlite3_reset(x -> stmt);
			outlet_bang(x -> status_outlet);
			return;
			
		} else if (rc == SQLITE_ROW) {
			int col_count, i, col_type;
			t_float col_float;
			
			col_count = sqlite3_column_count(x -> stmt);
			x -> results_count = 0;
			for (i = 0; i < col_count; i++) {
				col_type = sqlite3_column_type(x -> stmt, i);
				if (x -> tagged) {
					char *col_name = (char *) sqlite3_column_name(x -> stmt, i);
					//post("col_name: '%s'", col_name);
					SETSYMBOL(&x -> results[x -> results_count], gensym(col_name));
					x -> results_count++;
				}
				switch(col_type) {
					case SQLITE_INTEGER:
						col_float = (t_float) (int) sqlite3_column_double(x -> stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_FLOAT:
						col_float = (t_float) sqlite3_column_double(x -> stmt, i);
						SETFLOAT(&x -> results[x -> results_count], col_float);
						x -> results_count++;
						break;
					case SQLITE_TEXT:
						{
							char *col_data = (char *) sqlite3_column_text(x ->stmt, i);
							SETSYMBOL(&x -> results[x -> results_count], gensym(col_data));
							x -> results_count++;
						}
						break;
					case SQLITE_BLOB:
					case SQLITE_NULL:
						SETSYMBOL(&x -> results[x -> results_count], gensym("<NULL>"));
						x -> results_count++;
						break;
					default:
						post("not sure what to do with this data type.");
						post("name:'%s' type:%d", (char *)sqlite3_column_name(x -> stmt, i), col_type);
				}
			}
			outlet_list(x -> x_obj.ob_outlet, gensym("list"), x -> results_count, &x -> results[0]);
		} else if (rc != SQLITE_OK) {
			sqlite3query_error(x, sqlite3_errmsg(x -> dbadmin -> db));
		} else {
			post("sqlite3query_bang: ok, what is this return code?");
		}
	} else {
		// this is here in case a statement is NOT prepared, and it has an [until]
		// object doing the iteration (preventing infinite loops)
		sqlite3query_error(x, "statement not prepared.");
		outlet_bang(x -> status_outlet);
	}
}

static void sqlite3query_reset(t_sqlite3query *x) {
	DEBUG(post("sqlite3query_reset"));
	if (x -> stmt) {
		sqlite3_reset(x -> stmt);
	}
}

/*
 *	This is used to insert any parameters for placed values. It is assumed that you know
 *	the number and the types of values needed.
 */
static void sqlite3query_anything(t_sqlite3query *x, t_symbol *s, int argc, t_atom *argv) {
	t_symbol *message = NULL;
	
	DEBUG(post("sqlite3query_anything: '%s' %d", s -> s_name, argc));
	
	// 1. if no stmt, raise error
	if (x -> stmt) {
		// 2. get count of expected parameters, raise error if not equal to argc
		int pcount = sqlite3_bind_parameter_count(x -> stmt);
		if (argc == pcount) {
			int i, rc;
			// 3. store each element from the list (or whatever type of float/symbol/or list of them)
			sqlite3_reset(x -> stmt);
			sqlite3_clear_bindings(x -> stmt);
			for (i = 0; i < argc; i++) {
				if (argv[i].a_type == A_FLOAT) {
					// post("float argv[%d] = %g", i, argv[i].a_w.w_float);
					
					rc = sqlite3_bind_double(x -> stmt, i + 1, (double) atom_getfloat(&argv[i]));
					if (rc != SQLITE_OK) {
						sqlite3query_error(x, sqlite3_errmsg(x -> dbadmin -> db));
						return;
					}
				} else if (argv[i].a_type == A_SYMBOL) {
					t_symbol *sym = atom_getsymbol(&argv[i]);
					
					rc = sqlite3_bind_text(x -> stmt, i + 1, sym -> s_name, -1, SQLITE_STATIC);
					if (rc != SQLITE_OK) {
						sqlite3query_error(x, sqlite3_errmsg(x -> dbadmin -> db));
						return;
					}
				} else {
					sqlite3query_error(x, "unsupported datatype in placeholders");
					return;
				}
			}
		} else {
			sqlite3query_error(x, "incorrect number of placeholder parameters");
		}
	} else {
		const char *err = NULL;
		if (!x -> dbadmin) {
			err = "no database connected.";
		} else {
			err = "statement not prepared.";
		}
		sqlite3query_error(x, err);
	}
}

static void sqlite3query_prepare(t_sqlite3query *x) {
	DEBUG(post("sqlite3query_prepare"));
	const char *tail;
	char *buf_text;
	int i, j, length;
	binbuf_gettext(x -> bbuf, &buf_text, &length);
	j = 0;
	for (i = 0; i < length; i++) {
		// it appears that when we get the SQL from the creation arguments, that
		// the comma's are not the same (they are A_SYMBOL, not A_COMMA).
		// so, as a result, we have to strip out the preceeding '\'
		// if there is a preceeding space, we also remove that
		if (buf_text[i] != '\\') {
			x -> sql_buffer[j++] = buf_text[i];
		} else if (x -> sql_buffer[j - 1] == ' ') {
			j--;
		}
	}
	x -> sql_buffer[length] = '\0';
	freebytes(buf_text, length);
	
	fprintf(stderr, "sql: '%s'\n", x -> sql_buffer);
	
	int rc = sqlite3_prepare_v2(x -> dbadmin -> db, x -> sql_buffer, -1, &x -> stmt, &tail);
	if (rc != SQLITE_OK) {
		sqlite3query_error(x, sqlite3_errmsg(x -> dbadmin -> db));
		return;
	}
	x -> dbadmin -> ref_count++;
}

static void sqlite3query_use(t_sqlite3query *x, t_symbol *db_key) {
	// 1. lookup key, if not found, raise error
	DEBUG(post("sqlite3query_use: '%s'", (db_key) ? db_key -> s_name : "NO-KEY"));
	t_sqlite3db *dbadmin = sqlite3library.db[sqlite3library_lookupdb(db_key)];
	
	// reset input flag, so that the next thing inserted deletes the contents first.
	x -> begin_chunk = sFALSE;
	if (dbadmin) {
		// 2. if db already connected and active statement, finalize and db = null
		sqlite3query_finalize(x);
		// 3. connect with new database, and prepare the statement
		x -> dbadmin = dbadmin;
		sqlite3query_prepare(x);
	} else {
		sqlite3query_error(x, "db-key not found");
	}
}

static void sqlite3query_free(t_sqlite3query *x) {
	int i;
	// finalize the statement
	sqlite3query_finalize(x);
	
	// remove query from library
	for (i = 0; i < STATEMENT_COUNT; i++) {
		if (sqlite3library.queries[i] == x) {
			sqlite3library.queries[i] = NULL;
		}
	}
}

static void sqlite3query_cold_init(t_sqlite3query_cold *p, t_sqlite3query *query) {
	p -> q_pd = sqlite3query_cold_class;
	p -> query = (void *)query;
}

static void sqlite3query_cold_anything(t_sqlite3query_cold *p, t_symbol *s, int argc, t_atom *argv) {
//	int i;
//	char buf[MAXPDSTRING];
	t_sqlite3query *x = (t_sqlite3query *)p -> query;
//	char *b = x -> sql_buffer;
	
	// post("sqlite3query_cold_anything: '%s'", s -> s_name);
	if (x -> stmt) {
		sqlite3_finalize(x -> stmt);
		x -> stmt = NULL;
//		strcpy(b, "");
	}
	collector_cold_anything((t_collector_cold *)p, s, argc, argv);
//	if (x -> begin_chunk) {
//		strcat(b, ", ");
//	} else {
//		x -> begin_chunk = sTRUE;
//	}
//	if (s != &s_float) {
//		strcat(b, s -> s_name);
//		strcat(b, " ");
//	}
//	for (i = 0; i < argc; i++) {
//		atom_string(&argv[i], buf, MAXPDSTRING);
//		strcat(b, buf);
//		if (i < (argc - 1)) {
//			strcat(b, " ");
//		}
//	}
}

static void *sqlite3query_new(t_symbol *s, int argc, t_atom *argv) {
	t_sqlite3query *x = (t_sqlite3query *)pd_new(sqlite3query_class);
	if (x) {
		int i;
		// store in library
		for (i = 0; i < STATEMENT_COUNT; i++) {
			if (sqlite3library.queries[i] == NULL) {
				break;
			}
		}
		if (i == STATEMENT_COUNT) {
			pd_free((void*)x);
			return NULL;
		}
		sqlite3library.queries[i] = x;
		
//		char buf[MAXPDSTRING];
//		strcpy(x -> sql_buffer, "");
//		if (argc > 0) {
//			for(i = 0; i < argc; i++) {
//				atom_string(&argv[i], buf, MAXPDSTRING);
//				if (strcmp(buf, "\\,") == 0) {
//					strcat(x -> sql_buffer, ",");
//				} else {
//					if (i > 0) {
//						strcat(x -> sql_buffer, " ");
//					}
//					strcat(x -> sql_buffer, buf);
//				}
//			}
//		}
		x -> bbuf = binbuf_new();
		binbuf_add(x -> bbuf, argc, argv);
		
		// set up inlets
		sqlite3query_cold_init(&x -> cold, x);
		inlet_new(&x -> x_obj, &x -> cold.q_pd, 0, 0);
		
		// set up outlets
		outlet_new(&x -> x_obj, &s_list);
		x -> error_outlet = outlet_new(&x -> x_obj, &s_list);
		x -> status_outlet = outlet_new(&x -> x_obj, 0);
	} else {
		post("can't init sqlite3query object.");
	}
	return x;
}

static void sqlite3rowid_use(t_sqlite3rowid *x, t_symbol *db_key) {
	DEBUG(post("sqlite3rowid_use: %s", db_key -> s_name));
	x -> db_index = sqlite3library_lookupdb(db_key);
}

static void sqlite3rowid_bang(t_sqlite3rowid *x) {
	t_sqlite3db *dbadmin = (x -> db_index >= 0) ? sqlite3library.db[x -> db_index] : NULL;
	DEBUG(post("sqlite3rowid_bang: %d", x -> db_index));
	if (dbadmin && dbadmin -> db) {
		t_float v = (t_float) (int) sqlite3_last_insert_rowid(dbadmin -> db);
		outlet_float(x -> x_obj.ob_outlet, v);
	}
}

static void *sqlite3rowid_new(t_symbol *s, int argc, t_atom *argv) {
	t_sqlite3rowid *x = (t_sqlite3rowid *)pd_new(sqlite3rowid_class);
	if (x) {
		x -> db_index = -1;
		outlet_new(&x -> x_obj, &s_float);
	}
	return x;
}

static void sqlite3blob_anything(t_sqlite3blob *x, t_symbol *s, int argc, t_atom *argv) {
	post("sqlite3blob_anything: '%s' %d", s -> s_name, argc);
}

static void *sqlite3blob_new(t_symbol *s, int argc, t_atom *argv) {
	t_sqlite3blob *x = (t_sqlite3blob *)pd_new(sqlite3blob_class);
	if (x) {
		int i;
		char buf[MAXPDSTRING];
		post("sqlite3blob_new: %s %d", s -> s_name, argc);
		for(i = 0; i < argc; i++) {
			atom_string(&argv[i], buf, MAXPDSTRING);
			post("  [%d]: %s", i, buf);
		}
	}
	return x;
}

static void sqlite3blob_free(t_sqlite3blob *x) {
	post("sqlite3blob_free");
}

void sqlite3db_setup(void) {
	int i;
	for (i = 0; i < DATABASE_COUNT; i++) { sqlite3library.db[i] = NULL; }
	for (i = 0; i < STATEMENT_COUNT; i++) { sqlite3library.queries[i] = NULL; }
	
	SETCOMMA(&comma_atom);
	
	collector_cold_class = class_new(gensym("cold"), NULL, NULL, sizeof(t_collector_cold), 0, A_GIMME, 0);
	class_addanything(collector_cold_class, (t_method)collector_cold_anything);
	
	collector_class = class_new(gensym("collector"), (t_newmethod)collector_new,
			(t_method)collector_free, sizeof(t_collector), 0, A_GIMME, 0);
	class_addbang(collector_class, (t_method)collector_bang);
	class_addmethod(collector_class, (t_method)collector_clear, gensym("clear"), 0);
	
	sqlite3db_class = class_new(gensym("sqlite3db"), (t_newmethod)sqlite3db_new, (t_method)sqlite3db_free,
							sizeof(t_sqlite3db), 0, A_GIMME, 0);
	class_addbang(sqlite3db_class, (t_method)sqlite3db_bang);
	
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_open, gensym("open"), A_SYMBOL, 0);
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_close, gensym("close"), 0);
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_rowid, gensym("rowid"), 0);
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_report, gensym("report"), 0);
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_tagged, gensym("tagged"), A_FLOAT, 0);
	class_addmethod(sqlite3db_class, (t_method)sqlite3db_clear, gensym("clear"), 0);
	
	sqlite3db_cold_class = class_new(gensym("sqlite3db_cold"), NULL, NULL, sizeof(t_sqlite3db_cold), 0, A_GIMME, 0);
	class_addanything(sqlite3db_cold_class, (t_method)sqlite3db_cold_anything);
	
	
	sqlite3query_class = class_new(gensym("sqlite3query"), (t_newmethod)sqlite3query_new, (t_method)sqlite3query_free,
							sizeof(t_sqlite3query), 0, A_GIMME, 0);
	class_addcreator((t_newmethod)sqlite3query_new, gensym("query"), A_GIMME, 0);
	
	class_addbang(sqlite3query_class, (t_method)sqlite3query_bang);
	class_addanything(sqlite3query_class, (t_method)sqlite3query_anything);
	class_addmethod(sqlite3query_class, (t_method)sqlite3query_use, gensym("use"), A_SYMBOL, 0);
	class_addmethod(sqlite3query_class, (t_method)sqlite3query_reset, gensym("reset"), 0);
	class_addmethod(sqlite3query_class, (t_method)sqlite3query_finalize, gensym("finalize"), 0);
	class_addmethod(sqlite3query_class, (t_method)sqlite3query_prepare, gensym("prepare"), 0);
	
	sqlite3query_cold_class = class_new(gensym("sqlite3query_cold"), NULL, NULL, sizeof(t_sqlite3query_cold), 0, A_GIMME, 0);
	class_addanything(sqlite3query_cold_class, (t_method)sqlite3query_cold_anything);
	
	sqlite3rowid_class = class_new(gensym("sqlite3rowid"), (t_newmethod)sqlite3rowid_new, NULL,
							sizeof(t_sqlite3rowid), 0, A_GIMME, 0);
	class_addcreator((t_newmethod)sqlite3rowid_new, gensym("rowid"), A_GIMME, 0);
	class_addbang(sqlite3rowid_class, (t_method)sqlite3rowid_bang);
	class_addmethod(sqlite3rowid_class, (t_method)sqlite3rowid_use, gensym("use"), A_SYMBOL, 0);
	
	sqlite3blob_class = class_new(gensym("sqlite3blob"), (t_newmethod)sqlite3blob_new, (t_method)sqlite3blob_free,
							sizeof(t_sqlite3blob), 0, A_GIMME, 0);
	class_addcreator((t_newmethod)sqlite3blob_new, gensym("blob"), A_GIMME, 0);
	class_addanything(sqlite3blob_class, (t_method)sqlite3blob_anything);
	
	
	post("SQLite3 Database Interface, v0.03 Michael J McGonagle, 2009");
}
