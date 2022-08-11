#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>

#define RESIZE(array_, size_)                               \
  do {                                                      \
    if ((array_).capacity >= (size_)) {                     \
      (array_).size = (size_);                              \
    } else {                                                \
      ptrdiff_t capacity_ = (array_).capacity * 2;          \
      if (capacity_ == 0) {                                 \
        capacity_ = 1;                                      \
      }                                                     \
      while (capacity_ < (size_)) { capacity_ *= 2; }       \
      ptrdiff_t const bytes_ = capacity_ *                  \
                               sizeof((array_).values[0]);  \
      void *values_ = malloc(bytes_);                       \
      if (values_ != NULL) {                                \
        memcpy(values_, (array_).values,                    \
               (array_).size * sizeof((array_).values[0])); \
        if ((array_).capacity > (array_).local_size) {      \
          free((array_).values);                            \
        }                                                   \
        (array_).capacity = capacity_;                      \
        (array_).size     = size_;                          \
        (array_).values   = values_;                        \
      }                                                     \
    }                                                       \
  } while (0)

#define DESTROY(array_)                            \
  do {                                             \
    if ((array_).capacity > (array_).local_size) { \
      free((array_).values);                       \
    }                                              \
  } while (0)

#define END_ZERO(array_)                   \
  do {                                     \
    ptrdiff_t const size_ = (array_).size; \
    RESIZE((array_), size_ + 1);           \
    (array_).values[size_] = 0;            \
    RESIZE((array_), size_);               \
  } while (0)

#define WRAP_STR(local_str_) \
  { .size = sizeof(local_str_) - 1, .values = (local_str_) }

enum {
  STATUS_OK       = 0,
  STATUS_FAIL     = -1,
  PATH_DELIM      = '/',
  TYPE_FILE       = 0,
  TYPE_FOLDER     = 1,
  TYPE_SOURCE     = 2,
  TYPE_LIBRARY    = 3,
  TYPE_EXECUTABLE = 4
};

typedef struct {
  ptrdiff_t   size;
  char const *values;
} str_t;

typedef struct {
  ptrdiff_t capacity;
  ptrdiff_t size;
  ptrdiff_t local_size;
  char     *values;
} char_array_t;

typedef struct dep_tree {
  ptrdiff_t    type;
  char_array_t path;
  struct {
    ptrdiff_t        capacity;
    ptrdiff_t        size;
    ptrdiff_t        local_size;
    struct dep_tree *values;
  } children;
} dep_tree_t;

void dep_tree_destroy(dep_tree_t tree) {
  for (ptrdiff_t i = 0; i < tree.children.size; i++)
    dep_tree_destroy(tree.children.values[i]);

  DESTROY(tree.path);
  DESTROY(tree.children);
}

ptrdiff_t path_count(str_t path) {
  ptrdiff_t count = 1;

  for (ptrdiff_t i = 0; i < path.size; i++)
    if (path.values[i] == PATH_DELIM)
      count++;

  return count;
}

str_t path_split(str_t path, ptrdiff_t index) {
  if (index < 0)
    return path_split(path, path_count(path) + index);

  ptrdiff_t offset = 0;
  ptrdiff_t count  = 0;

  for (ptrdiff_t i = 0; i <= path.size; i++)
    if (i == path.size || path.values[i] == PATH_DELIM) {
      if (count == index) {
        str_t s = { .size   = i - offset,
                    .values = path.values + offset };
        return s;
      }

      offset = i + 1;
      count++;
    }

  str_t s = { .size = 0, .values = NULL };
  return s;
}

int enum_folder(str_t path, void *user_data,
                void eval(str_t name, void *user_data)) {
  DIR *directory = opendir(path.values);

  if (directory == NULL)
    return STATUS_FAIL;

  for (;;) {
    struct dirent *entry = readdir(directory);

    if (entry == NULL)
      break;

    if (strcmp(".", entry->d_name) == 0)
      continue;
    if (strcmp("..", entry->d_name) == 0)
      continue;

    if (eval != NULL) {
      str_t const name = { .size   = strlen(entry->d_name),
                           .values = entry->d_name };
      eval(name, user_data);
    }
  }

  closedir(directory);

  return STATUS_OK;
}

dep_tree_t eval_folder(str_t path);

typedef struct {
  dep_tree_t   tree;
  char_array_t next;
} eval_data_t;

void eval_folder_back(str_t name, void *user_data) {
  eval_data_t *data = (eval_data_t *) user_data;

  ptrdiff_t const n = data->tree.children.size;
  RESIZE(data->tree.children, n + 1);

  RESIZE(data->next, data->tree.path.size + name.size + 1);
  memcpy(data->next.values + data->tree.path.size + 1, name.values,
         name.size);
  END_ZERO(data->next);

  str_t const next_str = { .size   = data->next.size,
                           .values = data->next.values };

  data->tree.children.values[n] = eval_folder(next_str);
}

dep_tree_t eval_folder(str_t path) {
  eval_data_t data;
  memset(&data, 0, sizeof data);

  data.tree.type = TYPE_FOLDER;

  RESIZE(data.tree.path, path.size);
  memcpy(data.tree.path.values, path.values, path.size);
  END_ZERO(data.tree.path);

  char buf[40];
  data.next.capacity   = sizeof(buf);
  data.next.local_size = sizeof(buf);
  data.next.values     = buf;

  RESIZE(data.next, path.size + 1);
  memcpy(data.next.values, path.values, path.size);
  data.next.values[path.size] = PATH_DELIM;

  if (enum_folder(path, &data, eval_folder_back) == STATUS_FAIL)
    data.tree.type = TYPE_FILE;

  DESTROY(data.next);
  return data.tree;
}

void print(dep_tree_t tree, int depth) {
  switch (tree.type) {
    case TYPE_FILE: printf(" : file   : "); break;
    case TYPE_FOLDER: printf(" : folder : "); break;
  }

  if (depth > 0)
    printf("%*c", depth, ' ');

  str_t path_full = { .size   = tree.path.size,
                      .values = tree.path.values };
  str_t path      = path_split(path_full, -1);

  printf("%.*s%*c: %s\n", path.size, path.values,
         20 - depth - path.size, ' ', tree.path.values);

  for (ptrdiff_t i = 0; i < tree.children.size; i++)
    print(tree.children.values[i], depth + 2);
}

int main(int argc, char **argv) {
  str_t const path = WRAP_STR(".");

  dep_tree_t tree = eval_folder(path);

  print(tree, 0);

  dep_tree_destroy(tree);

  return STATUS_OK;
}
