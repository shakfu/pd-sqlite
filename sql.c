#include "m_pd.h"

#include <sqlite3.h>

#define SYMBOL_LENGTH 100
// #define DEBUG(x) x
#define DEBUG(x)


static t_class *sql_class;


typedef struct _sql {
    t_object  x_obj;

    // sqlite-specific
    sqlite3 *db;

} t_sql;

// class methods
// ---------------------------------------------------------------------------
void sql_symbol(t_sql *x, t_symbol *s) {
    post("s: %s", s->s_name);

    sqlite3_stmt *res;
    
    int rc = sqlite3_open(":memory:", &x->db);
    
    if (rc != SQLITE_OK) {
        
        pd_error(x, "Cannot open database: %s", sqlite3_errmsg(x->db));
        sqlite3_close(x->db);
        
        return;
    }
    
    // rc = sqlite3_prepare_v2(x->db, "SELECT SQLITE_VERSION()", -1, &res, 0);  
    rc = sqlite3_prepare_v2(x->db, s->s_name, -1, &res, 0);  
    
    if (rc != SQLITE_OK) {
        
        pd_error(x, "Failed to fetch data: %s", sqlite3_errmsg(x->db));
        sqlite3_close(x->db);
        
        return;
    }    
    
    rc = sqlite3_step(res);
    
    if (rc == SQLITE_ROW) {
        post("version: %s", sqlite3_column_text(res, 0));
    }
    
    sqlite3_finalize(res);
    sqlite3_close(x->db);
    
}


void sql_bang(t_sql *x)
{
    DEBUG(post("this is a debugging statement."));
    post("%s\n", sqlite3_libversion()); 
}

// class constructor, destructor and setup
// ---------------------------------------------------------------------------
void *sql_new(void)
{
    t_sql *x = (t_sql *)pd_new(sql_class);

    return (void *)x;
}

// void sql_free(t_sql *x) {
//     sqlite3_close(x->db);
// }


void sql_setup(void) {

    sql_class = class_new(gensym("sql"),
                               (t_newmethod)sql_new,
                               0, // (t_method)sql_free, 
                               sizeof(t_sql),
                               CLASS_DEFAULT,
                               0);

    class_addbang(sql_class, sql_bang);
    class_addsymbol(sql_class, sql_symbol);

}

