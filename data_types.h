#ifndef DATA_TYPES_H_INCLUDED
#define DATA_TYPES_H_INCLUDED

#include <stdio.h>
#include <stdint.h>

enum {
    DB_HEADER_SIZE = 100,
    DB_HDR_PAGE_SIZE_OFF = 16,
    DB_HDR_WRITE_VER_OFF = 18,
    DB_HDR_READ_VER_OFF = 19,
    DB_HDR_RESERVED_SPACE_OFF = 20,
    DB_HDR_FILE_CHANGE_CNT_OFF = 24,
    DB_HDR_SCHEMA_COOKIE_OFF = 40,
    DB_HDR_SCHEMA_FORMAT_OFF = 44,
};

typedef struct {
    FILE *fp;
    uint32_t page_size;
    uint32_t pages_count;
} db_file_t;

typedef struct {
    FILE *fp;
    uint32_t page_size;
    uint32_t pages_count;
} db_file_t;

typedef struct {
    uint16_t page_size; // at header offset 16 (big-endian); 1 means 65536
    uint8_t write_version; // offset 18
    uint8_t read_version; // offset 19
    uint8_t reserved_space; // offset 20
    uint32_t schema_cookie; // offset 40 (big-endian)
    uint32_t schema_format; // offset 44 (big-endian)
    uint32_t file_change_counter; // offset 24 (big-endian)
} db_header_t;

typedef enum {
    BTREE_PAGE_INTERIOR_INDEX = 0x02,
    BTREE_PAGE_INTERIOR_TABLE = 0x05,
    BTREE_PAGE_LEAF_INDEX = 0x0a,
    BTREE_PAGE_LEAF_TABLE = 0x0d
} btree_page_type_t;

typedef struct {
    btree_page_type_t type;
    uint16_t cell_count;
    uint16_t cell_content_offset;
    uint16_t fragmented_free_bytes;
    uint32_t rightmost_child; /* only valid for interior pages */
    uint16_t header_size; /* 8 or 12 */
} btree_page_header_t;

typedef struct {
    uint32_t page_no; /* 1-based */
    uint32_t page_size;
    uint8_t *raw; /* full page buffer */
    uint8_t *payload; /* points into raw */
    uint8_t header_offset; /* 0 or 100 (first page)*/
    btree_page_header_t header;
} db_page_t;

typedef struct {
    const uint8_t *payload;
    uint32_t payload_size;
} cell_record_t;

typedef struct {
    char *type; /* "table", "index", ... */
    char *name; /* object name */
    char *tbl_name; /* table name */
    uint32_t root_page; /* root B-tree page */
    char *sql; /* CREATE statement (optional) */
} schema_entry_t;

#endif // DATA_TYPES_H_INCLUDED
