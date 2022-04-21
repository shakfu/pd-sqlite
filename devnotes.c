
// definition
t_binbuf *bbuf;
int begin_chunk;

// initialization
x->bbuf = binbuf_new();


// data input
static void collector_cold_anything(t_collector_cold *c, t_symbol *s, int argc,
                                    t_atom *argv) {
    t_collector *x = (t_collector *)c->collector;
    t_binbuf *b = x->bbuf;

    DEBUG(post("cold_anything:"));
    if (x->begin_chunk) {
        binbuf_add(b, 1, &comma_atom);
    } else {
        x->begin_chunk = sTRUE;
    }
    if (s != &s_float) {
        t_atom sel;
        SETSYMBOL(&sel, s);
        binbuf_add(b, 1, &sel);
    }
    binbuf_add(b, argc, argv);
}


// clear
static void collector_clear(t_collector *x) {
    DEBUG(post("collector_clear"));
    binbuf_clear(x->bbuf);
    x->begin_chunk = sFALSE;
}


// free
static void collector_free(t_collector *x) {
    if (x->bbuf) {
        binbuf_free(x->bbuf);
    }
}


// output
static void collector_bang(t_collector *x) {
    DEBUG(post("collector_bang"));
    outlet_anything(x->x_obj.ob_outlet, &s_list, binbuf_getnatom(x->bbuf),
                    binbuf_getvec(x->bbuf));


static void sqlite3db_bang(t_sqlite3db *x) {
    DEBUG(post("sqlite3db_bang"));

    if (x->stmt) {
        t_symbol *message = NULL;
        // this means that we have an active statement, so we just step it
        int rc = sqlite3_step(x->stmt);
        DEBUG(post("sqlite3db_bang: stepping"));
        if (rc == SQLITE_DONE) {
            sqlite3_finalize(x->stmt);
            x->stmt = NULL;
            outlet_bang(x->status_outlet);
            sqlite3db_clear(x);
            return;
        } else if (rc == SQLITE_ROW) {
            int col_count, i, col_type;
            t_float col_float;

            col_count = sqlite3_column_count(x->stmt);
            x->results_count = 0;
            for (i = 0; i < col_count; i++) {
                col_type = sqlite3_column_type(x->stmt, i);
                if (x->tagged) {
                    char *col_name = (char *)sqlite3_column_name(x->stmt, i);
                    SETSYMBOL(&x->results[x->results_count], gensym(col_name));
                    x->results_count++;
                }
                switch (col_type) {
                case SQLITE_INTEGER:
                    col_float = (t_float)(int)sqlite3_column_double(x->stmt, i);
                    SETFLOAT(&x->results[x->results_count], col_float);
                    x->results_count++;
                    break;
                case SQLITE_FLOAT:
                    col_float = (t_float)sqlite3_column_double(x->stmt, i);
                    SETFLOAT(&x->results[x->results_count], col_float);
                    x->results_count++;
                    break;
                case SQLITE_TEXT: {
                    char *col_data = (char *)sqlite3_column_text(x->stmt, i);
                    SETSYMBOL(&x->results[x->results_count], gensym(col_data));
                    x->results_count++;
                } break;
                case SQLITE_BLOB:
                case SQLITE_NULL:
                    SETSYMBOL(&x->results[x->results_count], gensym("<NULL>"));
                    x->results_count++;
                    break;
                default:
                    post("not sure what to do with this data type.");
                    post("name:'%s' type:%d",
                         (char *)sqlite3_column_name(x->stmt, i), col_type);
                }
            }
            message = gensym("results");
        } else if (rc != SQLITE_OK) {
            sqlite3db_error(x, sqlite3_errmsg(x->db));
            return;
        }
        outlet_anything(x->x_obj.ob_outlet, message, x->results_count,
                        &x->results[0]);
        //	} else if (x->db && strlen(x->sql_buffer) > 0) {
    } else if (x->db && binbuf_getnatom(x->bbuf) > 0) {
        // we have some awaiting sql to process
        const char *tail;
        char *buf_text;
        int i, length;

        binbuf_gettext(x->bbuf, &buf_text, &length);
        // while binbuf appears to use 'strcpy', it may not return a null
        // terminated string
        for (i = 0; i < length; i++) {
            x->sql_buffer[i] = buf_text[i];
        }
        x->sql_buffer[length] = '\0';
        // also, I think we need to free the string returned by 'binbuf_gettext'
        freebytes(buf_text, length);

        fprintf(stderr, "sql: '%s'\n", x->sql_buffer);

        int rc = sqlite3_prepare_v2(x->db, x->sql_buffer, -1, &x->stmt, &tail);
        if (rc != SQLITE_OK) {
            sqlite3db_error(x, sqlite3_errmsg(x->db));
            return;
        }
        x->sql_processed = sTRUE;
        sqlite3db_bang(x);
        //	} else {
        //		const char *err = "doing nothing";
        //		x->errors_count = sqlite3library_error_to_atoms(&x ->
        //error[0], err); 		outlet_anything(x->x_obj.ob_outlet, gensym("error"),
        //x->errors_count, &x->error[0]);
    }
}


