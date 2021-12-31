/*
 *	an SQLquery is a client object of an SQLdb object. an SQLdb implements
 *	the connection to an SQL database, and is a named PD object. See SQLdb.c
 *	for more info.
 *
 *	an SQLquery object has two different methods of inputing SQL . You do not
 *	need to know both to use the object. The first method only takes the
 *	following creation arguments:
 *
 *		[query server-id]
 *
 *	where 'server-id' is the named server to post the queries with. This
 *	method creates a second inlet that is used to insert the SQL into the
 *	buffer. This may be your primary method of handling SQl.
 *
 *	The second method takes the SQL at the creation of an instance, as follows:
 *
 *		[query server-id insert into mytable (id, name) values (?, ?)]
 *
 *	the 'server-id' is the same as above, and the remaining symbols are interpreted
 *	as being the SQL statement to implement. each '?' in the statement is connected
 *	to an inlet for this instance
 *
 *	CAUTION: Don't get too fancy with your statements, as the parser only looks for
 *	'?' characters, and isn't too smart about quoted things. It is expected that the
 *	input SQL is the responsibility of the user.
 */
#include <stdio.h>
#include <string.h>

#include "m_pd.h"

static t_class *SQLquery_class;
static t_class *SQLquery_proxy_class;	// for cold inlet lists

typedef struct _SQLquery_proxy_class {
	t_pd l_pd;
	
	void *q;
} t_SQLquery_proxy_class;

typedef struct _SQLquery {
	t_object x_obj;
	
	t_SQLquery_proxy_class pxy;		// this handles the arbitary cold inlet
	
	t_symbol *id;			// this is how we are known to PD
	t_symbol *server;		// the PD symbol for where we send our SQL
	
	t_int submit_flag;		// indicates that a new submission needs to be made
	
	char sqlb[MAXPDSTRING];		// the input SQL buffer
	int begin_chunk;			// controls comma reinsertion at the start of a message block
	
	int tagged;				// do we want the result sets to be tagged with their field names?
	t_outlet *rowid_outlet;		// this outlets a float that is the last rowid inserted
	t_outlet *status_outlet;	// this is where the database status gets sent
	
	t_atom message[3];		// what we send to the server
} t_SQLquery;

/*
 *	In a bang, we either submit a statement, or we process its result sets.
 *	With each subsequent bangs, we return the next result set.
 *
 *	To prevent the same buffer from being submitted more than once, we use
 *	'x -> submit_flag' to control if the buffer should be sent as a 'query'
 *	or a 'next'.
 *
 *	This will eventually cover the "placeholder" implementation, but that depends
 *	on knowing the datatypes for each inlet to create.
 */
static void SQLquery_bang(t_SQLquery *x) {
	post("SQLquery_bang");
	
	if (x -> server -> s_thing) {
		int argc;
		t_symbol *mess;
		
		if (x -> submit_flag) {
			argc = 2;
			// a pointer to the text buffer for the sql, we make this first
			// because of how 'tagged' is checked. I don't want this to be
			// subjected to any class tests, as this is a (a c-string buffer
			// cast to a 't_gpointer *').
			SETPOINTER(&x -> message[0], (t_gpointer *) x -> sqlb);
			SETSYMBOL(&x -> message[1], x -> id);
			if (x -> tagged) {
				argc = 3;
				SETSYMBOL(&x -> message[2], gensym("tagged"));
			}
			x -> submit_flag = 0;
			mess = gensym("query");
		} else {
			argc = 1;
			SETSYMBOL(&x -> message[0], x -> id);
			if (x -> tagged) {
				argc = 2;
				SETSYMBOL(&x -> message[1], gensym("tagged"));
			}
			mess = gensym("next");
		}
		post("SQLquery_bang: %s", mess -> s_name);
		pd_typedmess(x -> server -> s_thing, mess, argc, &x -> message[0]);
	}
}

static void SQLquery_addcomma(t_SQLquery *x) { strcat(x -> sqlb, ","); }
static void SQLquery_addsemi(t_SQLquery *x) { strcat(x -> sqlb, ";"); }
static void SQLquery_adddollar(t_SQLquery *x) { strcat(x -> sqlb, "$"); }

static void SQLquery_clear(t_SQLquery *x) {
	strcpy(x -> sqlb, "");
	
	x -> begin_chunk = 0;
	x -> submit_flag = 1;
}

static void SQLquery_chunk(t_SQLquery *x) {
	x -> begin_chunk = 0;
}

static void SQLquery_tagged(t_SQLquery *x, t_float f) {
	if (f != 0.0) {
		x -> tagged = 1;
	} else {
		x -> tagged = 0;
	}
}

static void SQLquery_lastid(t_SQLquery *x) {
	SETSYMBOL(&x -> message[0], x -> id);
	pd_typedmess(x -> server -> s_thing, gensym("lastid"), 1, &x -> message[0]);
}

/*
 *	these are messages sent to us back from the server, to handle result sets,
 *	rowids, and any error messages.
 */
/*
 *	As the error message coming back from the server is already in a list, we just
 *	output it on the status_outlet.
 */
static void SQLquery_error(t_SQLquery *x, t_symbol *s, int argc, t_atom *argv) {
	post("SQLquery_error");
	outlet_list(x -> status_outlet, &s_list, argc, argv);
}

/*
 *	If a result set is empty, it is assumed to be an empty set, thus the end of
 *	the whole set of results. You can think of an [until] object controlling the
 *	result sets, and the bang sent out the status_outlet is connected to the second
 *	inlet of the [until], stopping it...
 
 [bang(													)]
  |    [r $0-stop-results]
  |    /
 [until]
  |
 [SQLquery <server-id>]
  |			|		 |
  |			|		[route bang]
  |			|		 |
  |			|		[s $-stop-results]
  |		   [v $0-rowid]
 [pd do-what-you-will-with-this-result-set-list]
 */
static void SQLquery_results(t_SQLquery *x, t_symbol *s, int argc, t_atom *argv) {
	post("SQLquery_results");
	if (argc > 0) {
		// we have something coming back
		outlet_list(x -> x_obj.ob_outlet, &s_list, argc, argv);
	} else {
		// there is nothing coming back, so we assume this is the end of the result set
		outlet_bang(x -> status_outlet);
	}
}

/*
 *	this returns the rowid (unique id) for the last inserted row
 */
static void SQLquery_rowid(t_SQLquery *x, t_symbol *s, int argc, t_atom *argv) {
	post("SQLquery_rowid");
	outlet_float(x -> rowid_outlet, atom_getfloatarg(0, argc, argv));
}

/*
 *	call this to initialize the proxy object
 */
static void SQLquery_proxy_init(t_SQLquery_proxy_class *p, t_SQLquery *q) {
	p -> l_pd = SQLquery_proxy_class;
	
	p -> q = (void *)q;
}

// Now declare the functions for the proxy class, so that it has
// access to the class structure
static void SQLquery_proxy_anything(t_SQLquery_proxy_class *p, t_symbol *s, int argc, t_atom *argv) {
	int i;
	char buf[MAXPDSTRING];
	char *b = ((t_SQLquery *)p -> q) -> sqlb;
	
	post("SQLquery_proxy_anything: '%s'", s -> s_name);
	if (((t_SQLquery *)p -> q) -> begin_chunk) {
		// this means that we have already started the buffer
		strcat(b, ", ");
	} else {
		((t_SQLquery *)p -> q) -> begin_chunk = 1;
	}
	if (s != &s_float) {
		strcat(b, s -> s_name);
		strcat(b, " ");
	}
	for(i = 0; i < argc; i++) {
		atom_string(&argv[i], buf, MAXPDSTRING);
		post("argv[%d]: '%s'", i, buf);
		
		strcat(b, buf);
		if (i < (argc - 1)) {
			strcat(b, " ");
		}
		if (strlen(b) > ((MAXPDSTRING / 6) * 5)) {
			post("sqlb almost full. (%d) (%d)", strlen(b), MAXPDSTRING);
		}
	}
}

/*
 * this will never be called. just here for a placeholder.
 */
static void *SQLquery_proxy_new(t_symbol *s, int argc, t_atom *argv) {
	return NULL;
}

/*
 *	this should be called from your free if you create something in init...
 */
static void SQLquery_proxy_free(t_SQLquery_proxy_class *p) {
	
}

/*
 *	[SQLquery <server_id> ...sql statement...] <- currently not implemented!!!!!!!!!!!!
 *		this will produce a query object that will use Placeholders to "inject" data
 *		into the compiled/submitted SQL statement.
 *		As SQLite supports the basic '?' syntax for placeholders, that will be supported here.
 *		BUT, we also need to know the datatypes that are expected for each of the inlets
 *		so that we can create those types (hopefully to eliminate the possibilities of
 *		using the software to mount an SQL inject attack on a database).
 *
 *	[SQLquery <server_id>] <- this creates a proxy inlet to accept arbitrary list input
 *		that is the SQL statement, or some chunk of it...
 *		When inputing your SQL, it is important to note that because we depend on ','s in
 *		the message (which PD will strip out), will be reinserted automatically, as each
 *		chunk of the message will be sent to this object instance. Therefore, if you
 *		are going to build SQL that is in fragments, you will need to be sure to send
 *		[chunk( )] for each chuck you append to the buffer
 *
 *			[select * from $1(							)] <- cold inlet
 *			[chunk(										)] <- hot inlet
 *			[where x = '$1'(							)] <- cold inlet
 *			[chunk(										)] <- hot inlet
 *			[limit $1 offset $2(						)] <- cold inlet
 *			[bang(										)] <- hot inlet (submits SQL)
 *
 *		of course, the above example could be done as
 *
 *			[select * from $1 where x = '$2' limit $3 offset $4(		)]
 *			[bang(														)]
 */
static void *SQLquery_new(t_symbol *s, int argc, t_atom *argv) {
	t_SQLquery *x = (t_SQLquery *)pd_new(SQLquery_class);
	if (x) {
		int i;
		char buf[100];
		
		// might need some explanation, as x is a unique address, we
		// use that to make a unique symbol for us
		sprintf(buf, "SQLquery_%ld", (long int)x);
		x -> id = gensym(buf);
		pd_bind(&x -> x_obj.ob_pd, x -> id);
		x -> server = atom_getsymbolarg(0, argc, argv);
		x -> submit_flag = 0;
		x -> begin_chunk = 0;
		x -> tagged = 0;
		
//		post("SQLquery_new: '%s' %d '%s' '%s'",
//			 s -> s_name, argc,
//			 (x -> server) ? x -> server -> s_name : "NULL",
//			 (x -> id) ? x -> id -> s_name : "NULL");
		
		for(i = 1; i < argc; i++) {
			atom_string(&argv[i], buf, 100);
			post("token[%d]: %s", i, buf);
		}
		
		SQLquery_proxy_init(&x -> pxy, x);
		inlet_new(&x -> x_obj, &x -> pxy.l_pd, 0, 0);
		
		outlet_new(&x -> x_obj, &s_list);
		x -> rowid_outlet = outlet_new(&x -> x_obj, &s_float);
		x -> status_outlet = outlet_new(&x -> x_obj, 0);
	}
	post("SQLquery_new: '%ld'", (long int)x);
	return x;
}

static void SQLquery_free(t_SQLquery *c) {
	pd_unbind(&c -> x_obj.ob_pd, c -> id);
}

void SQLquery_setup(void) {
	post("SQLquery_setup");
	SQLquery_proxy_class = class_new(gensym("SQLquery_proxy_class"),
							   (t_newmethod)SQLquery_proxy_new,
							   (t_method)SQLquery_proxy_free,
							   sizeof(t_SQLquery_proxy_class),
							   0,
							   A_GIMME,
							   0);
	class_addanything(SQLquery_proxy_class, (t_method)SQLquery_proxy_anything);
	
	SQLquery_class = class_new(gensym("SQLquery"),
							 (t_newmethod)SQLquery_new,
							 (t_method)SQLquery_free,
							 sizeof(t_SQLquery),
							 0,
							 A_GIMME,
							 0);
	class_addbang(SQLquery_class, (t_method)SQLquery_bang);
	
	class_addmethod(SQLquery_class, (t_method)SQLquery_error, gensym("error"), A_GIMME, 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_results, gensym("results"), A_GIMME, 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_lastid, gensym("lastid"), 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_rowid, gensym("rowid"), A_GIMME, 0);
	
	class_addmethod(SQLquery_class, (t_method)SQLquery_tagged, gensym("tagged"), A_DEFFLOAT, 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_addcomma, gensym("addcomma"), 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_adddollar, gensym("adddollar"), 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_addsemi, gensym("addsemi"), 0);
	class_addmethod(SQLquery_class, (t_method)SQLquery_clear, gensym("clear"), 0);
}
