#include "database_handler.h"
#include "data_types.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQLITE_HEADER_SIZE 100

static uint32_t read_varint(const uint8_t *p, uint64_t *out) {
    uint64_t v = 0;
    int i = 0;
    for (; i < 9; i++) {
        v = (v << 7) | (p[i] & 0x7F);
        if ((p[i] & 0x80) == 0) {
            *out = v;
            return (uint32_t) (i + 1);
        }
    }
    *out = v;
    return i;
}

static int read_varint(const unsigned char *buf, size_t max_len, uint64_t *value, size_t *bytes_read) {
    uint64_t v = 0;
    size_t i;

    for (i = 0; i < max_len && i < 9; ++i) {
        unsigned char byte = buf[i];
        if (i == 8) {
            v = (v << 8) | byte;
            *value = v;
            *bytes_read = 9;
            return 1;
        }

        v = (v << 7) | (uint64_t)(byte & 0x7F);
        if ((byte & 0x80) == 0) {
            *value = v;
            *bytes_read = i + 1;
            return 1;
        }
    }
    return 0;
}

static uint8_t *get_page_cells_array_ptr(const db_page_t *p) {
    return p->payload;
}

static uint8_t *page_cell_ptr(const db_page_t *p, const uint16_t cell_idx) {
    return p->raw + get_page_cell_offset(p, cell_idx);
}

// Read a 2-byte big-endian integer
static uint16_t read_uint16_be(const unsigned char *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

// Read a 4-byte big-endian integer
static uint32_t read_uint32_be(const unsigned char *buf)
{
    return ((uint32_t)buf[0] << 24)
        | ((uint32_t)buf[1] << 16)
        | ((uint32_t)buf[2] << 8)
        | (uint32_t)buf[3];
}

static uint32_t serial_len(const uint64_t s) {
    if (s == 0) return 0;
    if (s == 1) return 1;
    if (s == 2) return 2;
    if (s == 3) return 3;
    if (s == 4) return 4;
    if (s == 5) return 6;
    if (s == 6) return 8;
    if (s == 8 || s == 9) return 0;
    if (s >= 12) return (uint32_t) ((s - 12) / 2);
    return 0;
}


static int serial_is_text(const uint64_t s) {
    return (s >= 13) && (s & 1);
}

static char *read_string(const uint8_t *const p, const uint32_t n)
{
    char* s = malloc(n+1);
    if (!s) return NULL;
    memcpy(s, p, n);
    s[n] = 0;
    return s;
}

static int get_db_size(FILE *fp, uint64_t *out_size)
{
    if (!fp || !out_size) return -1;
    const long cur = ftell(fp); // return current file position of given stream
    if (cur < 0) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    const long end = ftell(fp);
    if (end < 0) return -1;
    if (fseek(fp, cur, SEEK_SET) != 0) return -1;
    *out_size = (uint64_t) end;
    return 0;
}

int db_open(db_file_t *db, const char *path)
{
    if (!db || !path) return -1;
    memset(db, 0, sizeof(*db));

    db->fp = fopen(path, "rb");
    if (!db->fp)
    {
        fprintf(stderr, "db_open: failed to open file %s\n", path);
        return -1;
    }

    db_header_t header;
    if (db_read_header(db, &header) != 0)
    {
        fprintf(stderr, "db_open: failed to read header\n");
        db_close(db);
        return -1;
    }

    db->page_size = header.page_size;
    uint64_t sz = 0;
    if (get_db_size_bytes(db->fp, &sz) == 0 && db->page_size != 0)
    {
        db->pages_count = (uint32_t) (sz / (uint64_t) db->page_size);
    } else
    {
        db->pages_count = 0;
    }
    return 0;
}

void db_close(db_file_t *db)
{
    if (!db) return;
    if (db->fp) fclose(db->fp);
    memset(db, 0, sizeof(*db));
}

int db_read_header(const db_file_t *db, db_header_t *header)
{
    if (!db || !db->fp || !header) return -1;
    memset(header, 0, sizeof(header));

    if (fseek(db->fp, 0, SEEK_SET) != 0) return -1;

    uint8_t buf[DB_HEADER_SIZE];
    const size_t n = fread(buf, 1, sizeof(buf), db->fp);
    if (n != sizeof(buf)) return -1;

    uint16_t ps = read_uint16_be(buf + DB_HDR_PAGE_SIZE_OFF);
    if (ps == 1) ps = (uint16_t) 65536;
    if (ps < 512 || (ps & (ps -1)) != 0) return -1;

    header->page_size = ps;
    header->write_version = buf[DB_HDR_WRITE_VER_OFF];
    header->read_version = buf[DB_HDR_READ_VER_OFF];
    header->reserved_space = buf[DB_HDR_RESERVED_SPACE_OFF];
    header->file_change_counter = read_u32_be(buf + DB_HDR_FILE_CHANGE_CNT_OFF);
    header->schema_cookie = read_u32_be(buf + DB_HDR_SCHEMA_COOKIE_OFF);
    header->schema_format = read_u32_be(buf + DB_HDR_SCHEMA_FORMAT_OFF);

    return 0;
}

void btree_parse_page_header(btree_page_header_t *h, const uint8_t *p)
{
   h->type = (btree_page_type_t) p[0];
   h->cell_count = read_uint16_be(p+3);
   h->cell_content_offset = read_uint16_be(p+5);
   h->fragmented_free_bytes = p[7];

   if (h->type == BTREE_PAGE_INTERIOR_TABLE || h->type == BTREE_PAGE_INTERIOR_INDEX)
    {
        h->rightmost_child = read_uint32_be(p+8);
        h->header_size = 12;
    } else {
        h->rightmost_child = 0;
        h->header_size = 8;
    }
}

int db_read_page(const db_file_t *const db, uint32_t page_num, uint8_t *buf, const size_t buf_len)
{
    if (!db || !db->fp || !buf) return -1;
    if (db->page_size == 0) return -1;
    if (page_num == 0) return -1;
    if (buf_len < db->page_size) return -1;

    const size_t page_size = db->page_size;
    const size_t pages_count = db->pages_count;

    if (pages_count != 0 && page_num > pages_count) return -1;
    const uint64_t offset = (uint64_t) (page_num - 1) * (uint64_t) page_size;

    if (fseek0(db->fp, (off_t) offset, SEEK_SET) != 0) return -1;

    const size_t nr = fread(buf, 1, page_size, db->fp);
    if (nr != page_size)
    {
        return -1;
    }

    return 0;
}

int db_read_page_t(const db_file_t *const db, uint32_t page_num, db_page_t *out)
{
    if (!db || !out || page_num == 0) return -1;
    memset(out, 0, sizeof(*out));

    out->page_no = page_num;
    out->page_size = db->page_size;
    out->raw = malloc(db->page_size);

    if (!out->raw) return -1;

    if (db_read_page(db, page_num, out->raw, db->page_size) != 0)
    {
        free(out->raw);
        return -1;
    }

    out->header_offset = (page_num == 1) ? DB_HEADER_SIZE : 0;
    const uint8_t *header = out->raw + out->header_offset;
    btree_parse_page_header(&out->header, header);
    out->payload = out->raw + out->header_offset + out->header.header_size;
    return 0;
}

void db_free_page_t(db_page_t *p)
{
    if (!p) return;
    free(p->raw);
    memset(p, 0, sizeof(*p));
}

static cell_record_t leaf_table_cell_record(const uint8_t *cell)
{
    uint64_t payload_sz = 0, rowid = 0;
    const uint32_t n1 = read_varint(cell, &payload_sz);
    const uint32_t n2 = read_varint(cell + n1, &rowid);
    cell_record_t r = { .payload = cell + n1 + n2, .payload_size = (uint32_t) payload_sz };
    return r;
}

static int read_schema_entry_from_cell(const uint8_t *cell, schema_entry_t *const out)
{
    memset(out, 0, sizeof(*out));
    const cell_record_t record = leaf_table_cell_record(cell);
    const uint8_t *payload = record.payload;
    uint64_t header_sz = 0;
    uint32_t off = read_varint(payload, &header_sz);
    uint64_t serials[5];

    for (unsigned int i = 0; i < 5; ++i)
    {
        uint64_t s = 0;
        uint32_t n = read_varint(payload + off, &s);
        if (n == 0) return -1;
        off += n;
        serials[i] = s;
    }
    // You can now use the serials array as needed
    assert(off <= header_sz);
    uint32_t data_off = (uint32_t) header_sz;

    // col 0: type (TEXT)
    if (serial_is_text(serials[0]))
    {
        const uint32_t len = serial_len(serials[0]);
        out->type = read_string(payload + data_off, len);
    }
    data_off += serial_len(serials[0]);

    // col 1: name (TEXT)
    if (serial_is_text(serials[1]))
    {
        const uint32_t len = serial_len(serials[1]);
        out->name = read_string(payload + data_off, len);
    }
    data_off += serial_len(serials[1]);

    // col 2: tbl_name (TEXT)
    if (serial_is_text(serials[2]))
    {
        const uint32_t len = serial_len(serials[2]);
        out->tbl_name = read_string(payload + data_off, len);
    }
    data_off += serial_len(serials[2]);

    // column 3: root_page (INTEGER)
    if (serials[3] == 8) out->root_page = 0;
    else if (serials[3] == 9) out->root_page = 1;
    else {
        const uint32_t len = serial_len(serials[3]);
        uint32_t v = 0;
        for (uint32_t i = 0; i < len; ++i)
        {
            v = (v << 8) | payload[data_off + i];
        }
        out->root_page = v;
    }
    data_off += serial_len(serials[3]);

    // column 4: sql (TEXT)
    if (serial_is_text(serials[4]))
    {
        const uint32_t len = (uint32_t) ((serials[4] - 13) / 2);
        out->sql = read_string(payload + data_off, len);
    }

    return out->type && out->name;
}

static int schema_is_internal(const schema_entry_t *s)
{
    if (!s || !s->type || !s->name) return 0;
    return (strncmp(s->name, "sqlite_", 7) == 0);
}

static void resolve_sqlite_schema(const db_file_t *const db, const uint32_t page_num,
    schema_entry_t **out, size_t *count, size_t *cap)
{
    db_page_t page;
    
    if (db_read_page_t(db, page_num, &page) != 0) return;

    if (page.header.type == BTREE_PAGE_LEAF_TABLE)
    {
        for (uint16_t i = 0; i < page.header.cell_count; i++)
        {
            schema_entry_t entry;
            if (read_schema_entry_from_cell(page_cell_ptr(&page, i), &entry))
            {
                if (schema_is_internal(&entry)) continue;
                if (strcmp(entry.type, "table") != 0) continue;
                if (*count == *cap)
                {
                    *cap = (*cap == 0) ? 16: *cap * 2;
                    *out = realloc(*out, *cap * sizeof(schema_entry_t));
                }
                (*out)[*count] = entry;
                (*count)++;
            }
        }
    } else if (page.header.type == BTREE_PAGE_INTERIOR_TABLE)
    {
        for (size_t i = 0; i < page.header.cell_count; i++)
        {
            const uint32_t child = read_uint32_be(page_cell_ptr(&page, i));
            resolve_sqlite_schema(db, child, out, count, cap);
        }
        resolve_sqlite_schema(db, page.header.rightmost_child, out, count, cap);
    }
    db_free_page_t(&page);
}

size_t db_load_schema(const db_file_t *const db, schema_entry_t **out)
{
    size_t count = 0, cap = 0;
    *out = NULL;
    resolve_sqlite_schema(db, 1U, out, &count, &cap);
    return count;
}

void print_table(const schema_entry_t *table, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (!strcmp(table[i].type, "table") && 
        strncmp(table[i].name, "sqlite_", 7) != 0)
        {
            printf("%s\n", table[i].name);
        }
    }
}

static FILE *open_database_file(const char *database_file_path, char *resolved_path, size_t resolved_path_size) {
    FILE *database_file = fopen(database_file_path, "rb");
    if (database_file) {
        snprintf(resolved_path, resolved_path_size, "%s", database_file_path);
        return database_file;
    }

    // When launched from bin/Debug, try parent folders for a workspace-root DB file.
    const char *fallback_prefixes[] = {"", "..\\", "..\\..\\"};
    size_t i;
    for (i = 0; i < sizeof(fallback_prefixes) / sizeof(fallback_prefixes[0]); ++i) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s%s", fallback_prefixes[i], database_file_path);
        database_file = fopen(candidate, "rb");
        if (database_file) {
            snprintf(resolved_path, resolved_path_size, "%s", candidate);
            return database_file;
        }
    }

    return NULL;
}

static size_t sqlite_serial_type_length(uint64_t serial_type) {
    if (serial_type == 0) return 0;
    if (serial_type == 1) return 1;
    if (serial_type == 2) return 2;
    if (serial_type == 3) return 3;
    if (serial_type == 4) return 4;
    if (serial_type == 5) return 6;
    if (serial_type == 6 || serial_type == 7) return 8;
    if (serial_type == 8 || serial_type == 9) return 0;
    if (serial_type >= 12) {
        if ((serial_type & 1ULL) == 0ULL) {
            return (size_t)((serial_type - 12ULL) / 2ULL); // BLOB
        }
        return (size_t)((serial_type - 13ULL) / 2ULL); // TEXT
    }
    return 0;
}

static int sqlite_serial_is_text(uint64_t serial_type) {
    return serial_type >= 13 && (serial_type & 1ULL) == 1ULL;
}

static int read_page_size(FILE *database_file, uint32_t *page_size_out) {
    unsigned char header[SQLITE_HEADER_SIZE];
    size_t bytes_read;
    uint32_t raw_page_size;

    if (fseek(database_file, 0, SEEK_SET) != 0) {
        return 0;
    }

    bytes_read = fread(header, 1, sizeof(header), database_file);
    if (bytes_read < sizeof(header)) {
        return 0;
    }

    raw_page_size = read_uint16_be(&header[16]);
    *page_size_out = (raw_page_size == 1U) ? 65536U : raw_page_size;
    return 1;
}

static int parse_schema_leaf_page(const unsigned char *page_buffer, uint32_t page_size, size_t page_header_offset, int *printed) {
    uint16_t num_cells;
    unsigned int i;

    if (page_header_offset + 8 > page_size) {
        return 0;
    }

    num_cells = read_uint16_be(&page_buffer[page_header_offset + 3]);
    for (i = 0; i < num_cells; ++i) {
        uint16_t cell_offset = read_uint16_be(&page_buffer[page_header_offset + 8 + i * 2]);
        const unsigned char *cell;
        const unsigned char *payload;
        size_t payload_limit;
        uint64_t payload_size = 0;
        uint64_t rowid = 0;
        size_t payload_size_len = 0;
        size_t rowid_len = 0;
        uint64_t header_size = 0;
        size_t header_size_len = 0;
        size_t header_cursor;
        size_t data_cursor;
        uint64_t serial_types[5];
        unsigned int col;
        char object_type[16] = {0};
        char object_name[256] = {0};

        if (cell_offset >= page_size) {
            continue;
        }

        cell = page_buffer + cell_offset;
        payload_limit = page_size - cell_offset;

        if (!read_varint(cell, payload_limit, &payload_size, &payload_size_len)) {
            continue;
        }
        if (!read_varint(cell + payload_size_len, payload_limit - payload_size_len, &rowid, &rowid_len)) {
            continue;
        }

        (void)rowid;
        payload = cell + payload_size_len + rowid_len;
        if ((size_t)(payload - page_buffer) >= page_size) {
            continue;
        }

        if (!read_varint(payload, page_size - (size_t)(payload - page_buffer), &header_size, &header_size_len)) {
            continue;
        }
        if (header_size < header_size_len) {
            continue;
        }

        header_cursor = header_size_len;
        for (col = 0; col < 5; ++col) {
            size_t serial_len = 0;
            if (header_cursor >= header_size) {
                break;
            }
            if (!read_varint(payload + header_cursor, header_size - header_cursor, &serial_types[col], &serial_len)) {
                break;
            }
            header_cursor += serial_len;
        }
        if (col < 5) {
            continue;
        }

        data_cursor = (size_t)header_size;
        for (col = 0; col < 5; ++col) {
            size_t field_len = sqlite_serial_type_length(serial_types[col]);
            const unsigned char *field_ptr = payload + data_cursor;

            if ((size_t)(field_ptr - page_buffer) + field_len > page_size) {
                break;
            }

            if (col == 0 && sqlite_serial_is_text(serial_types[col])) {
                size_t copy_len = field_len < sizeof(object_type) - 1 ? field_len : sizeof(object_type) - 1;
                memcpy(object_type, field_ptr, copy_len);
                object_type[copy_len] = '\0';
            }

            if (col == 1 && sqlite_serial_is_text(serial_types[col])) {
                size_t copy_len = field_len < sizeof(object_name) - 1 ? field_len : sizeof(object_name) - 1;
                memcpy(object_name, field_ptr, copy_len);
                object_name[copy_len] = '\0';
            }

            data_cursor += field_len;
        }

        if (strcmp(object_type, "table") == 0 && strncmp(object_name, "sqlite_", 7) != 0) {
            printf("%s\n", object_name);
            *printed = 1;
        }
    }

    return 1;
}

static int traverse_schema_btree(FILE *database_file, uint32_t page_size, uint32_t page_number, int *printed) {
    unsigned char *page_buffer;
    size_t page_header_offset;
    unsigned char page_type;
    uint16_t num_cells;
    unsigned int i;
    long page_file_offset;

    if (page_number == 0) {
        return 0;
    }

    page_buffer = (unsigned char *)malloc(page_size);
    if (!page_buffer) {
        return 0;
    }

    page_file_offset = (long)((page_number - 1U) * page_size);
    if (fseek(database_file, page_file_offset, SEEK_SET) != 0 || fread(page_buffer, 1, page_size, database_file) != page_size) {
        free(page_buffer);
        return 0;
    }

    page_header_offset = (page_number == 1U) ? SQLITE_HEADER_SIZE : 0;
    if (page_header_offset >= page_size) {
        free(page_buffer);
        return 0;
    }

    page_type = page_buffer[page_header_offset];
    if (page_type == 0x0D) {
        int ok = parse_schema_leaf_page(page_buffer, page_size, page_header_offset, printed);
        free(page_buffer);
        return ok;
    }

    if (page_type == 0x05) {
        uint32_t right_most_child;

        if (page_header_offset + 12 > page_size) {
            free(page_buffer);
            return 0;
        }

        num_cells = read_uint16_be(&page_buffer[page_header_offset + 3]);
        for (i = 0; i < num_cells; ++i) {
            uint16_t cell_offset = read_uint16_be(&page_buffer[page_header_offset + 12 + i * 2]);
            uint32_t left_child;

            if (cell_offset + 4 > page_size) {
                continue;
            }

            left_child = read_uint32_be(&page_buffer[cell_offset]);
            if (left_child != 0 && !traverse_schema_btree(database_file, page_size, left_child, printed)) {
                free(page_buffer);
                return 0;
            }
        }

        right_most_child = read_uint32_be(&page_buffer[page_header_offset + 8]);
        if (right_most_child != 0 && !traverse_schema_btree(database_file, page_size, right_most_child, printed)) {
            free(page_buffer);
            return 0;
        }

        free(page_buffer);
        return 1;
    }

    free(page_buffer);
    return 0;
}


int read_sqlite_schema(const char *database_file_path) {
    char resolved_path[1024];
    FILE *database_file = open_database_file(database_file_path, resolved_path, sizeof(resolved_path));
    if (!database_file) {
      fprintf(stderr, "Failed to open database file: %s\n", database_file_path);
      fprintf(stderr, "Tip: run with a full path or from the project root.\n");
            return 2;
    }
    unsigned char header[110];
    size_t bytes_read = fread(header, 1, sizeof(header), database_file);
    if (bytes_read < sizeof(header)) {
        fprintf(stderr, "Failed to read database header\n");
                fclose(database_file);
                return 3;
    }
        fclose(database_file);
    unsigned int raw_page_size = read_uint16_be(&header[16]);
    unsigned int page_size = (raw_page_size == 1U) ? 65536U : raw_page_size;

    // Page 1 starts at byte 100. In the B-tree page header, bytes 3-4 are the cell count.
    // For page 1 in this buffer, that maps to offsets 103-104.
    unsigned int schema_cell_count = read_uint16_be(&header[103]);
    printf("database file: %s\n", resolved_path);
    printf("database page size: %u\n", page_size);
    printf("number of schema objects: %u\n", schema_cell_count);
    return 0;
}

int print_user_name_tables(const char *database_file_path)
{
    char resolved_path[1024];
    FILE *database_file = open_database_file(database_file_path, resolved_path, sizeof(resolved_path));
    uint32_t page_size;
    int printed = 0;

    if (!database_file) {
        fprintf(stderr, "Failed to open database file: %s\n", database_file_path);
        return 2;
    }

    if (!read_page_size(database_file, &page_size)) {
        fclose(database_file);
        fprintf(stderr, "Failed to read SQLite header\n");
        return 1;
    }

    if (!traverse_schema_btree(database_file, page_size, 1U, &printed)) {
        fclose(database_file);
        fprintf(stderr, "Failed to traverse sqlite_schema B-tree\n");
        return 1;
    }
    fclose(database_file);

    if (!printed) {
        fprintf(stderr, "No user tables found\n");
    }

    return 0;
}
