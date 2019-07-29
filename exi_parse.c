// Copyright (c) 2015 Electric Power Research Institute, Inc.
// author: Mark Slicker <mark.slicker@gmail.com>

/** @addtogroup parse
    @{
*/

/** @brief Initialize the Parser to parse an EXI document.

    @param p is a pointer to a Parser object
    @param schema is the schema to use
    @param data is the beginning of the EXI stream
    @param length is the length of the stream in bytes
*/
void exi_parse_init (Parser *p, const Schema *schema, char *data, int length);

/** @} */

#ifndef HEADER_ONLY

/* The EXI bit-stream is encoded within a byte-stream, if the number of bytes
   needed from the byte stream is not met return 0.
*/
#define need(p, n) if ((p->end - p->ptr) < (n)) return 0

int parse_byte (uint8_t *b, Parser *p) {
  if (p->bit) { need (p, 2);
    *b = *p->ptr++ << p->bit;
    *b |= *p->ptr >> (8-p->bit);
  } else { need (p, 1);
    *b = *p->ptr++;
  } return 1;
}

// parse unsigned integers up to 70 (7x10) bits
int parse_uint (Parser *p) { uint8_t b;
  if (!p->ux_n) p->ux = 0;
  do {
    if (p->ux_n == 70) return p->state = PARSE_INVALID, 0;
    ok_v (parse_byte (&b, p), 0);
    p->ux |= (b & 0x7f) << p->ux_n; p->ux_n += 7;
  } while (b & 0x80);
  p->ux_n = 0; return 1;
}

/* parse binary data encoded as an unsigned integer and the same number of
   bytes, the length must be less than or equal to the predefined length by 
   the schema */
int parse_binary (Parser *p, uint8_t *b, uint64_t n) {
  uint64_t m;
  switch (p->exi_state) {
  case 0: // length
    ok_v (parse_uint (p), 0); p->exi_state++;
    if (p->ux > n) { p->state = PARSE_INVALID; return 0; }
  case 1: m = p->ux; // data
    need (p, p->bit? p->ux+1 : p->ux);
    m = p->ux; b += n-m;
    while (m--) parse_byte (b++, p);
    p->exi_state = 0; return 1;
  }
}

int parse_bit (int *bit, Parser *p) { need (p, 1);
  *bit = (*p->ptr >> (7-p->bit++)) & 1; 
  if (p->bit & 8) { p->ptr++; p->bit = 0; }
  return 1;
}

// parse n bits from the bit stream
int parse_bits (uint32_t *result, Parser *p, int n) {
  int bit = p->bit + n, bytes = bit >> 3; // number of bytes to fetch
  uint32_t bits = *p->ptr; bit &= 7; // final bit offset
  need (p, bit? bytes+1 : bytes); p->bit = bit;
  while (bytes--) bits = (bits << 8) | *(++p->ptr);
  *result = (bits >> (8-bit)) & ~(-1 << n); return 1;
}

// parse a signed integer (bounded range has greater than 4096 values)
int parse_integer (Parser *p) { int sign;
  switch (p->exi_state) {
  case 0: // sign bit
    ok_v (parse_bit (&sign, p), 0);
    p->sign = sign; p->exi_state++;
  case 1: // uint
    ok_v (parse_uint (p), 0);
    p->exi_state = 0;
    if (p->sign) p->sx = -p->ux;
  } return 1;
}

void parse_literal (Parser *p, char *s, int n) {
  while (n--) parse_uint (p), s = utf8_encode (s, p->ux); *s = '\0';
}

// parse compact id and look up string in the string table
int parse_compact_id (Parser *p, StringTable *t, void *value, int n) {
  if (t && t->index) { int id;
    ok_v (parse_bits (&id, p, bit_count (t->index-1)), 0);
    if (id < t->index) {
      char *s = t->strings[id];
      if (n) { if (strlen (s)+1 <= n) { strcpy (value, s); return 1; }
      } else { *(char **)value = strdup (s); return 1; } 
    } 
  } p->state = PARSE_INVALID; return 0;
}

// length of a utf-8 string (encoded as an exi string)
int exi_utf8_length (Parser *p, int n) {
  uint8_t *ptr = p->ptr; int length = 0;
  while (n) { int m = 0;  uint8_t b;
    /* UTF-8 code points need at most 21 bits (3 bytes in EXI encoding) and
       are encoded using 1 to 4 bytes in UTF-8. */
    do {
      if (!parse_byte (&b, p)) { p->ptr = ptr; return 0; }
      if (++m > 3) { p->state = PARSE_INVALID; return 0; }
    } while (b & 0x80);
    // adjust encoded length based upon bits needed
    switch (m) {
    case 2: if (b & 0x70) m++; break; // more than 11 bits needed?
    case 3: if (b & 0x7c) m++; break; // more than 16 bits needed?
    } length += m; n--;
  } p->ptr = ptr; return length;
}

// parse an EXI string, either a compact identifier or string literal
int exi_parse_string (Parser *p, void *value, int n) {
  const SchemaEntry *se = p->se; StringTable *t;
  const char *name; char *s; int m, length;
  switch (p->exi_state) {
  case 0: // value encoding
    ok_v (parse_uint (p), 0); p->exi_state++;
  case 1: // string value
    name = se_name (se, p->schema);
    switch (p->ux) {
    case 0: // local value lookup
      ok_v (parse_compact_id (p, find_table (p->local, name), value, n), 0);
      break;
    case 1: // global value lookup
      ok_v (parse_compact_id (p, p->global, value, n), 0); break;
    default: // literal value encoding
      length = p->ux - 2;
      ok_v (m = exi_utf8_length (p, length), 0);
      if (n) { // string is stored in a fixed container
	if (m >= n) { p->state = PARSE_INVALID; return 0; }
	s = value;
      } else { *(char **)value = s = malloc (m+1); }
      parse_literal (p, s, length);
      t = find_table (p->local, name);
      if (!t) t = p->local = new_string_table (p->local, name, 8);
      p->local = add_string (p->local, t, s);
      p->global = add_string (p->global, p->global, s);
    } p->exi_state = 0; return 1;
  }
}

#define pack_signed(dest, p) \
  (parse_integer (p)? *dest = p->sx, 1 : 0)
#define pack_unsigned(dest, p) \
  (parse_uint (p)? *dest = p->ux, 1 : 0)

int exi_parse_value (Parser *p, void *value) {
  int type = p->se->type, n; uint32_t x;
  type ^= ST_SIMPLE; n = type >> 4;
  switch (type & 0xf) {
  case XS_STRING: return exi_parse_string (p, value, n);
  case XS_BOOLEAN:
    return parse_bit (&n, p)? *(uint32_t *)value |= n << p->flag, 1 : 0;
  case XS_HEX_BINARY: return parse_binary (p, value, n);
  case XS_ANY_URI: return exi_parse_string (p, value, 0);
  case XS_LONG: return pack_signed ((int64_t *)value, p);
  case XS_INT: return pack_signed ((int32_t *)value, p);
  case XS_SHORT: return pack_signed ((int16_t *)value, p);
  case XS_BYTE: return parse_bits (&x, p, 8)? *(int8_t *)value = x - 128, 1 : 0;
  case XS_ULONG: return pack_unsigned ((uint64_t *)value, p);
  case XS_UINT: return pack_unsigned ((uint32_t *)value, p);
  case XS_USHORT: return pack_unsigned ((uint16_t *)value, p);
  case XS_UBYTE: return parse_bits (&x, p, 8)? *(uint8_t *)value = x, 1 : 0;
  } return 0;
}

#undef pack_signed
#undef pack_unsigned

/* IEEE 2030.5-2017 EXI options document in XML format
  <header xmlns="http://www.w3.org/2009/exi">
    <common><schemaId>S1</schemaId></common>
  </header> */
/* IEEE 2030.5-2017 EXI options document in EXI format (bitstream)
   0 (header) | 01 (common) | 10 (schemaID) | 0 (CH)
   | 0x04 (literal string length=2) | 0x53 0x31 ("S1") | 1 (EE) */

/* IEEE 2030.5 uses a fixed set of options, so we only check for the header
   and the options document as defined above. */
int exi_parse_header (Parser *p) { int c;
  if (memcmp (p->ptr, "$EXI", 4) == 0) p->ptr += 4;
  need (p, 5); // need 5 bytes
  c = *p->ptr++; // EXI header is one byte in this case
  // 10 (distinguishing bits) | 1 (options present) | 00000 (version = 1)
  if (c == 0xa0) {
    char schemaId[64];
    uint32_t x; int n;
    parse_bits (&x, p, 6);
    parse_uint (p); n = p->ux;
    if (n && n < 64) { n -= 2;
      parse_literal (p, schemaId, n);
      // verify options document
      if (x == 0xc && streq (schemaId, p->schema->schemaId))
	return parse_bit (&n, p), n; // EE
    }
  } p->state = PARSE_INVALID; return 0;
}

// parse the header and the first event code (the global element)
int exi_parse_start (Parser *p) {
  if (!exi_parse_header (p)) return 0;
  ok_v (parse_bits (&p->type, p, bit_count (p->schema->length)), 0);
  if (p->type < p->schema->length) {
    p->se = &p->schema->entries[p->type];
    p->need_token = 1; return 1;
  } p->state = PARSE_INVALID; return 0;
}

 int exi_parse_simple (Parser *p, void *value) { int n;
  switch (p->ch) {
  case 0: // CH
    ok_v (parse_bit (&n, p), 0); p->ch++;
    if (n) { p->ch++; goto empty; }
  case 1: // content
    ok_v (exi_parse_value (p, value), 0);
    return p->ch = 0, 1;
  case 2: empty: // empty content
    ok_v (parse_bits (&n, p, 3), 0); 
    if (n) return p->state = PARSE_INVALID, 0;
    // EE is a second level code in this context
    return p->ch = 0, p->need_token = 0, 1;
  } return 0;
}

int exi_event (Parser *p, const SchemaEntry *se, int count) {
  if (p->need_token) {
    p->n = !se->n || (count < se->min)? 1 : se->n;
    ok_v (parse_bits (&p->token, p, bit_count (p->n)), 0);
    p->need_token = 0;
  } return 1;
}

int exi_parse_next (Parser *p) {
  if (p->se->n) {
    ok_v (exi_event (p, p->se, 0), 0);
    if (p->token >= p->n)
      return p->state = PARSE_INVALID, 0;
    p->se += p->token;
    if (p->se->n)
      return p->need_token = 1, p->state = PARSE_ELEMENT, 1;
  } p->state = PARSE_END; return 1;
}

int exi_xsi_type (Parser *p) {
  int n, type;
  switch (p->exi_state) {
  case 0: // event code
    ok_v (exi_event (p, p->se, 0), 0);
    if (p->token == p->n) // extended code
      p->exi_state++;
    else if (p->token > p->n)
      goto invalid;
    else return -1;
  case 1: // xsi:type
    ok_v (parse_bits (&n, p, 3), 0);
    if (n != 0) goto invalid;
    p->exi_state++;
  case 2: // URI - targetNamespace
    ok_v (parse_bits (&n, p, 3), 0);
    if (n != 5) goto invalid;
    p->exi_state++;
  case 3: // compact id
    ok_v (parse_uint (p), 0);
    if (p->ux != 0) goto invalid;
    p->exi_state++;  
  case 4: // local name
    n = bit_count (p->schema->count);
    ok_v (parse_bits (&n, p, n), 0);
    if (n >= p->schema->count
	|| !(type = p->schema->types[n]))
      goto invalid;
    p->exi_state = 0;
    return type;
  }
 invalid:
  p->state = PARSE_INVALID; return 0;
}

int exi_parse_end (Parser *p, const SchemaEntry *se) {
  if (p->need_token) { int n;
    ok_v (parse_bit (&n, p), 0);
    if (n) return p->state = PARSE_INVALID, 0;
  } else p->need_token = 1; return 1;
}

int exi_parse_sequence (Parser *p, StackItem *t) {
  if (exi_event (p, t->se, t->count)) {
    if (p->token) p->token--, p->state++;
    else return p->need_token = 1, 1;
  } return 0;
}

void exi_parse_done (Parser *p) {
  free_tables (p->global); free_tables (p->local);
}

void exi_rebuffer (Parser *p, char *data, int length) {
  p->ptr = data; p->end = data + length; p->truncated = 0;
}
 
const ParserDriver exi_parser = {
  exi_parse_start, exi_parse_next, exi_xsi_type, exi_parse_end,
  exi_parse_sequence, exi_parse_value, exi_parse_simple,
  exi_parse_done, exi_rebuffer
};

void exi_parse_init (Parser *p, const Schema *schema,
		     char *data, int length) {
  memset (p, 0, sizeof (Parser));
  exi_rebuffer (p, data, length);
  p->schema = schema; p->driver = &exi_parser;
  p->global = new_string_table (NULL, NULL, 32);
}

#endif
