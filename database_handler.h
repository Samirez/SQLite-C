#ifndef DATABASE_HANDLER_H_INCLUDED
#define DATABASE_HANDLER_H_INCLUDED

#include "data_types.h"

int read_sqlite_schema(const char *database_file_path);
int print_user_name_tables(const char *database_file_path);

/* Database file operations */
int db_open(db_file_t *db, const char *path);
void db_close(db_file_t *db);
int db_read_header(const db_file_t *db, db_header_t *hdr);

/* Page operations */
void btree_parse_page_header(btree_page_header_t *h, const uint8_t *p);
int db_read_page(const db_file_t *db, uint32_t page_num, uint8_t *buf, size_t buf_len);
int db_read_page_t(const db_file_t *db, uint32_t page_num, db_page_t *out);
void db_free_page_t(db_page_t *p);

/* Schema operations */
size_t db_load_schema(const db_file_t *db, schema_entry_t **out);
void print_table(const schema_entry_t *s, size_t n);


#endif // DATABASE_HANDLER_H_INCLUDED
