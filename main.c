#include "database_handler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static int cmp_table_by_table_name(const void *a, const void *b)
{
    const schema_entry_t *ea = a;
    const schema_entry_t *eb = b;
    if (!ea->tbl_name && !eb->tbl_name) return 0;
    if (!ea->tbl_name) return -1;
    if (!eb->tbl_name) return 1;

    return strcmp(ea->tbl_name, eb->tbl_name);
}

int check_sql_condition(const char *condition) {
    char query[256];
    snprintf(query, sizeof(query), "SELECT * FROM %s;", condition);
    if (strstr(query, "DROP") || strstr(query, "DELETE") || strstr(query, "UPDATE") || strstr(query, "INSERT")) {
        return 0; // Unsafe command detected
    }
    return 1; // Safe command
}

int main(int argc, char *argv[]) {
    char db_buf[512];
    char cmd_buf[64];
    const char *db_file_path;
    const char *command;
    char condition_string[] = "Y";

    if (argc == 3) {
        db_file_path = argv[1];
        command = argv[2];
    } else {
        printf("Enter input in the format: <database path> <command>\n");
        if (scanf("%511s %63s", db_buf, cmd_buf) != 2) {
            fprintf(stderr, "Failed to read input\n");
            return 1;
        }
        db_file_path = db_buf;
        command = cmd_buf;
    }

    db_file_t db;
    if (db_open(&db, db_file_path) != 0) {
        fprintf(stderr, "Failed to open database file: %s\n", db_file_path);
        return 1;
    }

    schema_entry_t *tables = NULL;
    const size_t tables_count = db_load_schema(&db, &tables);
    int exit_code = 0;

   if (strcmp(command, ".dbinfo") == 0)
    {
        int schema_read_result = read_sqlite_schema(db_file_path);
        if (schema_read_result != 0) {
            fprintf(stderr, "read_sqlite_schema failed with code %d\n", schema_read_result);
            exit_code = 1;
            goto cleanup;
        }
    }
    else if (strcmp(command, ".tables") == 0)
    {
        qsort(tables, tables_count, sizeof(schema_entry_t), cmp_table_by_table_name);
        for (size_t i = 0; i < tables_count; i++) {
            if (tables[i].tbl_name == NULL) continue;
            printf("%s\n", tables[i].tbl_name);
        }
        goto cleanup;
    } 
    else if (strcmp(command, ".schema") == 0) 
    {
            for (size_t i = 0; i < tables_count; i++) 
            {
                if (tables[i].tbl_name == NULL) continue;
                printf("%s\n", tables[i].sql ? tables[i].sql : "NULL");
            }
        exit_code = 1;
        goto cleanup;
    }
    else if (!check_sql_condition(command))
    {
        fprintf(stderr, "Unsafe command detected: %s\n", command);
        exit_code = 1;
        goto cleanup;
    } else if (check_sql_condition(command) && strcmp(command, "Y") == 0) {
        fprintf(stderr, "Condition is true, but no action defined for this case.\n");
        exit_code = 1;
        goto cleanup;
    }
    else {
        const char *last_space = strrchr(command, ' ');
        const char *table_name = (last_space != NULL) ? (last_space + 1) : command;

        int found = 0;
        for (size_t i = 0; i < tables_count; i++)
        {
            if (tables[i].tbl_name == NULL) continue;
            if (strcmp(tables[i].tbl_name, table_name) == 0)
            {
                found = 1;
                db_page_t table_page;
                if (db_read_page_t(&db, tables[i].root_page, &table_page) != 0)
                {
                    fprintf(stderr, "Failed to read table page for table: %s\n", tables[i].tbl_name);
                    exit_code = 1;
                    goto cleanup;
                }
                printf("%zu\n", table_page.header.cell_count);
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Table not found: %s\n", table_name);
            exit_code = 1;
            goto cleanup;
        }    
    }

cleanup:
    free(tables);
    db_close(&db);
    return exit_code;
}
