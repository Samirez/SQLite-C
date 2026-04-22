#include "database_handler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

static int cmp_table_by_table_name(const void *a, const void *b)
{
    const schema_entry_t *ea = a;
    const schema_entry_t *eb = b;
    if (!ea->tbl_name && !eb->tbl_name) return 0;
    if (!ea->tbl_name) return -1;
    if (!eb->tbl_name) return 1;

    return strcmp(ea->tbl_name, eb->tbl_name);
}

static int check_sql_condition(const char *command)
{
    char lower_command[1024];
    size_t i;

    if (!command) return 0;

    i = 0;
    while (command[i] != '\0' && i < sizeof(lower_command) - 1) {
        lower_command[i] = (char)tolower((unsigned char)command[i]);
        i++;
    }
    lower_command[i] = '\0';

    if (strstr(lower_command, "drop") || strstr(lower_command, "delete") || strstr(lower_command, "update") || strstr(lower_command, "insert")) {
        return 0;
    }
    return 1;
}

static int extract_table_name_from_command(const char *command, char *out, size_t out_size)
{
    const char *name = command;
    const char *from;
    size_t len;
    size_t i;

    if (!command || !out || out_size == 0) return 0;

    from = NULL;
    for (i = 0; command[i] != '\0'; i++) {
        if (tolower((unsigned char)command[i]) == 'f' &&
            tolower((unsigned char)command[i + 1]) == 'r' &&
            tolower((unsigned char)command[i + 2]) == 'o' &&
            tolower((unsigned char)command[i + 3]) == 'm' &&
            command[i + 4] == ' ') {
            from = command + i;
            break;
        }
    }
    if (from) {
        name = from + 5;
    }

    while (*name == ' ' || *name == '\t') {
        name++;
    }

    if (*name == '\0') return 0;

    len = strcspn(name, " ;\t\r\n");
    if (len == 0 || len >= out_size) return 0;

    memcpy(out, name, len);
    out[len] = '\0';
    return 1;
}

int main(int argc, char *argv[]) {
    char db_buf[512];
    char cmd_buf[1024];
    const char *db_file_path;
    const char *command = NULL;

    if (argc >= 3) {
        size_t pos = 0;
        int i;
        db_file_path = argv[1];

        cmd_buf[0] = '\0';
        for (i = 2; i < argc; i++) {
            size_t part_len = strlen(argv[i]);
            if (pos + part_len + ((i > 2) ? 1 : 0) >= sizeof(cmd_buf)) {
                fprintf(stderr, "Command too long\n");
                return 1;
            }
            if (i > 2) {
                cmd_buf[pos++] = ' ';
            }
            memcpy(cmd_buf + pos, argv[i], part_len);
            pos += part_len;
            cmd_buf[pos] = '\0';
        }
        command = cmd_buf;
    } else {
        printf("Enter input in the format: <database path> <command>\n");
        if (scanf("%511s", db_buf) != 1) {
            fprintf(stderr, "Failed to read input\n");
            return 1;
        }
        if (scanf(" %1023[^\n]", cmd_buf) != 1) {
            fprintf(stderr, "Failed to read command\n");
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
    }
    else {
        char table_name[256];

        if (!extract_table_name_from_command(command, table_name, sizeof(table_name))) {
            fprintf(stderr, "Failed to parse table name from command: %s\n", command);
            exit_code = 1;
            goto cleanup;
        }

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
