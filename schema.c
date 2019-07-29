// Copyright (c) 2015 Electric Power Research Institute, Inc.
// author: Mark Slicker <mark.slicker@gmail.com>

/** @defgroup schema Schema

    Defines the Schema and SchemaEntry data types, provides utility functions
    for Schema defined objects.
    @{
*/

enum XsType {XS_NULL, XS_STRING, XS_BOOLEAN, XS_HEX_BINARY, XS_ANY_URI,
	     XS_LONG, XS_INT, XS_SHORT, XS_BYTE, XS_ULONG, 
	     XS_UINT, XS_USHORT, XS_UBYTE};

#define ST_SIMPLE 0x8000 // simple schema type
#define xs_type(b, l) (((l)<<4)|(b))
#define is_boolean(xs) (((xs)^ST_SIMPLE) == XS_BOOLEAN)

typedef struct {
  int type;
  void *data;
} SubstitutionType;

typedef struct {
  union {
    unsigned short offset;
    unsigned short size;
  };
  union {
    unsigned short type;
    unsigned short index;
  };
  unsigned char min, max, n;
  unsigned int bit : 5;
  unsigned int st: 1;
  unsigned int attribute : 1;
  unsigned int unbounded : 1;
} SchemaEntry;

typedef struct _Schema {
  const char *namespace;
  const char *schemaId;
  const int length;
  const int count;
  const char * const *names;
  const uint16_t *types;
  const SchemaEntry *entries;
  const char * const *elements;
  const uint16_t *ids;
} Schema;

int se_is_a (const SchemaEntry *se, int base, const Schema *schema);

/** @brief Is a type derived from another type within a schema?
    @param type is the derived type
    @param base is a base type
    @param schema is a pointer to a Schema
    @returns 1 if type is derived from base, 0 otherwise
*/
int type_is_a (int type, int base, const Schema *schema);

/** @brief What is the size of an object of a given type?
    @param type is a schema type
    @param schema is a pointer to a Schema
    @returns the size of the schema typed object in bytes 
*/
int object_size (int type, const Schema *schema);

/** @brief Return a string representation of a type.
    @param type is a schema type
    @param schema is a pointer to a Schema
    @returns the type name as a string
*/
const char *type_name (int type, const Schema *schema);

/** @brief Free an object's elements without freeing the object container.
    @param obj is a pointer to a schema typed object 
    @param type is the type of the object
    @param schema is a pointer to the Schema
*/
void free_object_elements (void *obj, int type, const Schema *schema);

/** @brief Free an object's elements and the container.
    @param obj is a pointer to a schema typed object 
    @param type is the type of the object
    @param schema is a pointer to the Schema
*/
void free_object (void *obj, int type, const Schema *schema);

/** @brief Replace one object for another.

    Free the elements of the destination object and copy the source object to
    the same location. Free the source object container.
    @param dest is the destination object
    @param src is the source object
    @param type is the schema type of the objects
    @param schema is a pointer to the Schema
 */
void replace_object (void *dest, void *src, int type, const Schema *schema);


#define element_name(index, schema) (schema)->elements[index]
#define element_type(index, schema) (schema)->entries[index].type

/** @} */

#ifndef HEADER_ONLY

// pointer types
int is_pointer (int type) {
  switch (type^ST_SIMPLE) {
  case XS_STRING: case XS_ANY_URI: return 1;
  } return 0;
}

int se_is_a (const SchemaEntry *se, int base, const Schema *schema) {
  if (base < schema->length)
    base = schema->entries[base].index;
  if (se->type & ST_SIMPLE) return 0;
  while (se->index) {
    if (se->index == base) return 1;
    se = &schema->entries[se->index];
  } return 0;
}

int type_is_a (int type, int base, const Schema *schema) {
  return se_is_a (&schema->entries[type], base, schema);
}

const char *se_name (const SchemaEntry *se, const Schema *schema) {
  int index = se - schema->entries;
  return index < schema->length? schema->elements[index]
    : schema->names[schema->ids[index - schema->length]];
}

int object_size (int type, const Schema *schema) {
  const SchemaEntry *se;
  if (type & ST_SIMPLE) { int n;
    type ^= ST_SIMPLE; n = type >> 4;
    switch (type & 0xf) {
    case XS_STRING: return n? n : sizeof (char *);
    case XS_BOOLEAN: return 0;
    case XS_HEX_BINARY: return n;
    case XS_ANY_URI: return sizeof (char *);
    case XS_LONG: return 8;
    case XS_INT: return 4;
    case XS_SHORT: return 2;
    case XS_BYTE: return 1;
    case XS_ULONG: return 8;
    case XS_UINT: return 4;
    case XS_USHORT: return 2;
    case XS_UBYTE: return 1;
    }
    return 0;
  }
  if (type < schema->length)
    return object_size (schema->entries[type].index, schema);
  return schema->entries[type].size;
}

void free_elements (void *obj, const SchemaEntry *se,
		    const Schema *schema) {
  while (1) { int i; void *element = obj + se->offset;
    if (se->type & ST_SIMPLE) {
      if (is_pointer (se->type)) {
	void **value = (void **)element; i = 0;
	while (i < se->max && *value) {
	  void *t = *value; free (t); value++; i++;
	}
      }
    } else if (se->st) {
      SubstitutionType *st = element;
      if (st->data) free_object (st->data, st->type, schema);
    } else if (se->n) {
      const SchemaEntry *first = &schema->entries[se->index];
      if (se->unbounded) { List *t, *l = *(List **)(element);
	while (l) {
	  t = l; l = l->next;
	  free_elements (t->data, first+1, schema);
	  if (t->data) free (t->data); free (t); }
      } else {
	for (i = 0; i < se->max; i++) {
	  free_elements (element, first+1, schema);
	  element += first->size; 
	}
      }
    } else return; se++;
  }
}

void free_object_elements (void *obj, int type, const Schema *schema) {
  const SchemaEntry *se;
  if (type < schema->length) {
    se = &schema->entries[type]; type = se->index;
  }
  se = &schema->entries[type+1];
  free_elements (obj, se, schema);
}

void free_object (void *obj, int type, const Schema *schema) {
  free_object_elements (obj, type, schema); free (obj);
}

void replace_object (void *dest, void *src, int type, const Schema *schema) {
  free_object_elements (dest, type, schema);
  memcpy (dest, src, object_size (type, schema)); free (src);
}

#endif
