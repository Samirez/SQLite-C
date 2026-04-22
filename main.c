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

int main(int argc, char *argv[]) {
    char db_buf[512];
    char cmd_buf[64];
    const char *db_file_path;
    const char *command;

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

   if (strcmp(command, ".dbinfo") == 0)
    {
        int schema_read_result = read_sqlite_schema(db_file_path);
        if (schema_read_result != 0) {
            fprintf(stderr, "read_sqlite_schema failed with code %d\n", schema_read_result);
            return 1;
        }
    }
    else if (strcmp(command, ".tables") == 0)
    {
        int tables_read_result = print_user_name_tables(db_file_path);
        if (tables_read_result != 0) {
            fprintf(stderr, "print_user_name_tables failed with code %d\n", tables_read_result);
            return 1;
        }
    } 
    else if (strcmp(command, ".schema") == 0) {
        // not implemented yet, but we can read the schema page and print its raw content for demonstration.
        return 1;
    } 
    else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}
