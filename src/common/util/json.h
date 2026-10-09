#pragma once
#include <stddef.h>

/** String handling is selected by the protocol that owns the document. */
typedef enum
{
    UTIL_JSON_TEXT_EXACT   = 0, /**< Preserve escaped controls; reject U+0000. */
    UTIL_JSON_TEXT_DISPLAY = 1  /**< Replace controls and DEL with spaces, including U+0000. */
} UtilJsonText;

/** Cursor over a bounded JSON document; input need not be null terminated. */
typedef struct
{
    const unsigned char* at;        /**< Next unread byte. */
    const unsigned char* end;       /**< Exclusive end. */
    UtilJsonText         text_mode; /**< Caller-owned string handling policy. */
} UtilJson;

/**
 * @brief Skip JSON whitespace.
 * @param j Parser cursor.
 */
void util_json_space(UtilJson* j);

/**
 * @brief Consume punctuation after whitespace.
 * @param j Cursor.
 * @param c Expected byte.
 * @return Nonzero on a match.
 */
int util_json_take(UtilJson* j, unsigned char c);

/**
 * @brief Decode a validated string, truncating only at UTF-8 boundaries.
 * @param j Cursor and caller-selected text handling policy.
 * @param out Optional destination; NULL validates without storing text.
 * @param capacity Destination bytes including terminator; must be positive for non-NULL out.
 * @return One on success, two if truncated, zero on invalid input.
 */
int util_json_string(UtilJson* j, char* out, size_t capacity);

/**
 * @brief Consume a literal after whitespace.
 * @param j Cursor.
 * @param word Literal spelling.
 * @return Nonzero on a match.
 */
int util_json_literal(UtilJson* j, const char* word);

/**
 * @brief Validate and skip an unknown value with bounded nesting.
 * @param j Cursor.
 * @param depth Current depth.
 * @return Nonzero on valid input.
 */
int util_json_skip(UtilJson* j, unsigned depth);
