// #include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "m_pd.h"

#include <sqlite3.h>


#define DEBUG(x) x
// #define DEBUG(x)


static t_class *sql3_class;


typedef enum {
    CLOSED,
    OPEN
} db_state;


typedef struct _sql3 {
    t_object  x_obj;

    // general
    const char *currentdir;

    // sqlite-specific
    char db_path[MAXPDSTRING];
    char sql_buffer[MAXPDSTRING];
    sqlite3 *db;
    db_state db_state;
} t_sql3;



// utility functions
// ---------------------------------------------------------------------------

void path_combine(char* destination, const char* path1, const char* path2)
{
    // function from stack overflow
    // https://stackoverflow.com/questions/3142365/combine-directory-and-file-path-c/3142698
    if(path1 == NULL && path2 == NULL) {
        strcpy(destination, "");;
    }
    else if(path2 == NULL || strlen(path2) == 0) {
        strcpy(destination, path1);
    }
    else if(path1 == NULL || strlen(path1) == 0) {
        strcpy(destination, path2);
    } 
    else {
        char directory_separator[] = "/";
#ifdef WIN32
        directory_separator[0] = '\\';
#endif
        const char *last_char = path1;
        while(*last_char != '\0')
            last_char++;        
        int append_directory_separator = 0;
        if(strcmp(last_char, directory_separator) != 0) {
            append_directory_separator = 1;
        }
        strcpy(destination, path1);
        if(append_directory_separator)
            strcat(destination, directory_separator);
        strcat(destination, path2);
    }
}

// forward declarations
// ---------------------------------------------------------------------------
void sql3_close(t_sql3 *x);
void sql3_free(t_sql3 *x);


// class methods (typed)
// ---------------------------------------------------------------------------
void sql3_symbol(t_sql3 *x, t_symbol *s) {
    post("s: %s", s->s_name);

    sqlite3_stmt *res;

    if (x->db_state != OPEN) {
        pd_error(x, "database needs to be open first");
        return;
    }

    int i, j;

    int length = strlen(s->s_name);
    char *buf = malloc(length * sizeof(char));

    strcpy(buf, s->s_name);

    // clear sql_buffer
    memset(x->sql_buffer, 0, MAXPDSTRING);

    j = 0;
    for (i = 0; i < length; i++) {
        // remove escape `\` required for commas
        // this escape cleaning code is from sql3db.c
        // written by Michael J McGonagle 
        if (buf[i] != '\\') {
            x->sql_buffer[j++] = buf[i];
        } else if (x->sql_buffer[j - 1] == ' ') {
            j--;
        }
    }
    x->sql_buffer[length] = '\0';
    free(buf);

    post("sql: '%s'", x->sql_buffer);

    // rc = sqlite3_prepare_v2(x->db, "SELECT SQLITE_VERSION()", -1, &res, 0);  
    int rc = sqlite3_prepare_v2(x->db, x->sql_buffer, -1, &res, 0);  

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
    
}


void sql3_bang(t_sql3 *x)
{
    DEBUG(post("this is a debugging statement."));
    post("%s\n", sqlite3_libversion()); 
}

// class methods (messages)
// ---------------------------------------------------------------------------

void sql3_close(t_sql3 *x) {
    sqlite3_close(x->db);
    x->db_state = CLOSED;
}

void sql3_open(t_sql3 *x, t_symbol *s) {

    if (s == gensym(":memory:")) {
        // x->db_path = s->s_name;
        strcpy(x->db_path, s->s_name);
    } else {
        if (s->s_name[0] != '/') {
            // assumes db is in same folder as external
            path_combine(x->db_path, x->currentdir, s->s_name);
        } else {
            // assumes absolute path is provided
            strcpy(x->db_path, s->s_name);
        }
    }
    DEBUG(post("db_path: %s", x->db_path));

    int rc = sqlite3_open(x->db_path, &x->db);
    
    if (rc != SQLITE_OK) {
        pd_error(x, "Cannot open database: %s", sqlite3_errmsg(x->db));
        sql3_close(x);
        return;
    }
    x->db_state = OPEN;
}




// class constructor, destructor and setup
// ---------------------------------------------------------------------------
void *sql3_new(void)
{
    t_sql3 *x = (t_sql3 *)pd_new(sql3_class);

    // initialize params
    x->currentdir = canvas_getcurrentdir()->s_name;
    DEBUG(post("project_path: %s", x->currentdir));
    strcpy(x->db_path, "");
    x->db_state = CLOSED;

    return (void *)x;
}

void sql3_free(t_sql3 *x) {
    sql3_close(x);
}


void sql3_setup(void) {

    sql3_class = class_new(gensym("sql3"),
                           (t_newmethod)sql3_new,
                           (t_method)sql3_free, 
                           sizeof(t_sql3),
                           CLASS_DEFAULT,
                           0);

    class_addbang(sql3_class, sql3_bang);
    class_addsymbol(sql3_class, sql3_symbol);

    class_addmethod(sql3_class, (t_method)sql3_open, gensym("open"), A_SYMBOL, 0);
    class_addmethod(sql3_class, (t_method)sql3_close, gensym("close"), 0);
}

