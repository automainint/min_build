#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
      (array_).capacity = 0;                       \
    }                                              \
  } while (0)

#define APPEND(array_, add_)                                       \
  do {                                                             \
    ptrdiff_t const offset_ = (array_).size;                       \
    RESIZE((array_), offset_ + (add_).size);                       \
    memcpy((array_).values + offset_, (add_).values, (add_).size); \
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

#define DYNAMIC_ARRAY(element_type_) \
  ptrdiff_t      capacity;           \
  ptrdiff_t      size;               \
  ptrdiff_t      local_size;         \
  element_type_ *values

enum {
  STATUS_OK       = 0,
  STATUS_FAIL     = -1,
  PATH_DELIM      = '/',
  TYPE_FILE       = 0,
  TYPE_FOLDER     = 1,
  TYPE_ROOT       = 2,
  TYPE_HEADER     = 3,
  TYPE_SOURCE     = 4,
  TYPE_LIBRARY    = 5,
  TYPE_EXECUTABLE = 6
};

typedef struct {
  ptrdiff_t   size;
  char const *values;
} str_t;

typedef struct {
  DYNAMIC_ARRAY(char);
} char_array_t;

typedef struct dep_node {
  ptrdiff_t    type;
  char_array_t path;
  char_array_t output;
  struct {
    DYNAMIC_ARRAY(struct dep_node);
  } children;
  struct {
    DYNAMIC_ARRAY(char_array_t);
  } dependencies;
} dep_node_t;

void dep_node_destroy(dep_node_t tree) {
  for (ptrdiff_t i = 0; i < tree.children.size; i++)
    dep_node_destroy(tree.children.values[i]);

  for (ptrdiff_t i = 0; i < tree.dependencies.size; i++)
    DESTROY(tree.dependencies.values[i]);

  DESTROY(tree.path);
  DESTROY(tree.output);
  DESTROY(tree.children);
  DESTROY(tree.dependencies);
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

    if (entry->d_name[0] == '.')
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

typedef struct {
  uint64_t sec;
  unsigned nsec;
} sec_nsec_t;

dep_node_t generate_tree(str_t path);

typedef struct {
  dep_node_t   tree;
  char_array_t next;
} eval_data_t;

void generate_tree_back(str_t name, void *user_data) {
  eval_data_t *data = (eval_data_t *) user_data;

  ptrdiff_t const n = data->tree.children.size;
  RESIZE(data->tree.children, n + 1);

  RESIZE(data->next, data->tree.path.size + name.size + 1);
  memcpy(data->next.values + data->tree.path.size + 1, name.values,
         name.size);
  END_ZERO(data->next);

  str_t const next_str = { .size   = data->next.size,
                           .values = data->next.values };

  data->tree.children.values[n] = generate_tree(next_str);
}

dep_node_t generate_tree(str_t path) {
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

  if (enum_folder(path, &data, generate_tree_back) == STATUS_FAIL)
    data.tree.type = TYPE_FILE;

  DESTROY(data.next);
  return data.tree;
}

int is_header(dep_node_t const *const tree) {
  return tree->path.size >= 2 &&
         tree->path.values[tree->path.size - 2] == '.' &&
         tree->path.values[tree->path.size - 1] == 'h';
}

int is_source(dep_node_t const *const tree) {
  return tree->path.size >= 2 &&
         tree->path.values[tree->path.size - 2] == '.' &&
         tree->path.values[tree->path.size - 1] == 'c';
}

int is_main(dep_node_t const *const tree) {
  str_t const s = { .size   = tree->path.size,
                    .values = tree->path.values };
  return strcmp("main.c", path_split(s, -1).values) == 0;
}

int has_main(dep_node_t const *const tree) {
  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    if (is_main(tree->children.values + i))
      return 1;
  return 0;
}

int has_code(dep_node_t const *const tree) {
  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    if (tree->children.values[i].type == TYPE_HEADER ||
        tree->children.values[i].type == TYPE_SOURCE)
      return 1;
  return 0;
}

int has_libs(dep_node_t const *const tree) {
  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    if (tree->children.values[i].type == TYPE_LIBRARY)
      return 1;
  return 0;
}

int has_exes(dep_node_t const *const tree) {
  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    if (tree->children.values[i].type == TYPE_EXECUTABLE)
      return 1;
  return 0;
}

int has_targets(dep_node_t const *const tree) {
  ptrdiff_t count = has_libs(tree);
  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    if (tree->children.values[i].type == TYPE_EXECUTABLE)
      count++;
  return count > 1;
}

char_array_t to_cached(str_t const path) {
  str_t const cache_s = WRAP_STR("./.build_cache/");

  char_array_t s;
  memset(&s, 0, sizeof s);

  RESIZE(s, cache_s.size + path.size);
  memcpy(s.values, cache_s.values, cache_s.size);

  for (ptrdiff_t i = 0; i < path.size; i++)
    if (path.values[i] == PATH_DELIM)
      s.values[cache_s.size + i] = '_';
    else
      s.values[cache_s.size + i] = path.values[i];

  END_ZERO(s);
  return s;
}

char_array_t to_obj(str_t const path, ptrdiff_t const depth) {
  str_t const prefix_s = WRAP_STR("./obj/");
  str_t const delim_s  = WRAP_STR("__");
  str_t const ext_s    = WRAP_STR(".o");

  char_array_t s;
  memset(&s, 0, sizeof s);

  APPEND(s, prefix_s);

  for (ptrdiff_t k = depth;; k++) {
    str_t const part = path_split(path, k);
    if (part.size == 0)
      break;
    if (part.values[0] == '.')
      continue;
    if (k > depth)
      APPEND(s, delim_s);
    APPEND(s, part);
  }

  APPEND(s, ext_s);

  END_ZERO(s);
  return s;
}

char_array_t to_lib(str_t const path, ptrdiff_t const depth) {
  str_t const prefix_s = WRAP_STR("./bin");
  str_t const delim_s  = WRAP_STR("/");
  str_t const lib_s    = WRAP_STR("lib");
  str_t const def_s    = WRAP_STR("out");
  str_t const ext_s    = WRAP_STR(".a");

  char_array_t s;
  memset(&s, 0, sizeof s);

  APPEND(s, prefix_s);

  for (ptrdiff_t k = depth;; k++) {
    str_t const part = path_split(path, k);
    if (part.size == 0)
      break;
    if (part.values[0] == '.')
      continue;
    APPEND(s, delim_s);
    if (path_split(path, k + 1).size == 0)
      APPEND(s, lib_s);
    APPEND(s, part);
  }

  if (path_split(path, depth).size == 0) {
    APPEND(s, delim_s);
    APPEND(s, lib_s);
    APPEND(s, def_s);
  }

  APPEND(s, ext_s);

  END_ZERO(s);
  return s;
}

char_array_t to_exe(str_t const path, ptrdiff_t const depth) {
  str_t const prefix_s = WRAP_STR("./bin");
  str_t const delim_s  = WRAP_STR("/");
  str_t const def_s    = WRAP_STR("out");
  str_t const ext_s    = WRAP_STR("");

  char_array_t s;
  memset(&s, 0, sizeof s);

  APPEND(s, prefix_s);

  for (ptrdiff_t k = depth;; k++) {
    str_t const part = path_split(path, k);
    if (part.size == 0)
      break;
    if (part.values[0] == '.')
      continue;
    APPEND(s, delim_s);
    APPEND(s, part);
  }

  if (path_split(path, depth).size == 0) {
    APPEND(s, delim_s);
    APPEND(s, def_s);
  }

  APPEND(s, ext_s);

  END_ZERO(s);
  return s;
}

void process_tree(dep_node_t *const tree) {
  if (is_header(tree))
    tree->type = TYPE_HEADER;
  else if (is_source(tree))
    tree->type = TYPE_SOURCE;

  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    process_tree(tree->children.values + i);

  if (tree->type != TYPE_FOLDER)
    return;
  if (has_targets(tree)) {
    tree->type = TYPE_ROOT;
    return;
  }

  if (has_main(tree)) {
    tree->type = TYPE_EXECUTABLE;
    return;
  }

  if (has_exes(tree)) {
    for (ptrdiff_t i = 0; i < tree->children.size; i++)
      if (tree->children.values[i].type == TYPE_EXECUTABLE)
        tree->children.values[i].type = TYPE_FOLDER;
    tree->type = TYPE_EXECUTABLE;
    return;
  }

  if (has_libs(tree) || has_code(tree)) {
    for (ptrdiff_t i = 0; i < tree->children.size; i++)
      if (tree->children.values[i].type == TYPE_LIBRARY)
        tree->children.values[i].type = TYPE_FOLDER;
    tree->type = TYPE_LIBRARY;
  }
}

void generate_outputs(dep_node_t *const tree, ptrdiff_t const depth) {
  str_t const path = { .size   = tree->path.size,
                       .values = tree->path.values };

  switch (tree->type) {
    case TYPE_SOURCE:
    case TYPE_LIBRARY:
    case TYPE_EXECUTABLE: DESTROY(tree->output);
  }

  switch (tree->type) {
    case TYPE_SOURCE: tree->output = to_obj(path, depth); break;
    case TYPE_LIBRARY: tree->output = to_lib(path, depth); break;
    case TYPE_EXECUTABLE: tree->output = to_exe(path, depth); break;
  }

  for (ptrdiff_t i = 0; i < tree->children.size; i++)
    generate_outputs(tree->children.values + i, depth);
}

ptrdiff_t eval_path_size(str_t const path) {
  ptrdiff_t i = 0;
  while (path_split(path, i).size != 0) i++;
  return i;
}

ptrdiff_t eval_depth_loop(dep_node_t const *const tree) {
  if (tree->children.size == 1)
    return 1 + eval_depth_loop(tree->children.values);
  return 0;
}

ptrdiff_t eval_depth(dep_node_t const *const tree) {
  str_t const s = { .size   = tree->path.size,
                    .values = tree->path.values };
  return eval_path_size(s) + eval_depth_loop(tree);
}

dep_node_t eval_folder(str_t path) {
  dep_node_t tree = generate_tree(path);

  process_tree(&tree);
  generate_outputs(&tree, eval_depth(&tree));

  return tree;
}

sec_nsec_t get_mod_time(str_t path) {
  struct stat s;
  sec_nsec_t  t;
  memset(&t, 0, sizeof t);
  if (stat(path.values, &s) == -1)
    return t;
  else
    t.sec = (uint64_t) s.st_mtim.tv_sec;
  t.nsec = s.st_mtim.tv_nsec;
  return t;
}

void print_dump(dep_node_t tree, int indent) {
  switch (tree.type) {
    case TYPE_FILE: printf(": file   : "); break;
    case TYPE_FOLDER: printf(": folder : "); break;
    case TYPE_ROOT: printf(": root   : "); break;
    case TYPE_HEADER: printf(": header : "); break;
    case TYPE_SOURCE: printf(": source : "); break;
    case TYPE_LIBRARY: printf(": lib    : "); break;
    case TYPE_EXECUTABLE: printf(": exe    : "); break;
    default: printf(": ?      : ");
  }

  if (indent > 0)
    printf("%*c", indent, ' ');

  str_t const path_full = { .size   = tree.path.size,
                            .values = tree.path.values };
  str_t const name      = path_split(path_full, -1);

  printf("%.*s", (int) name.size, name.values);
  if (indent + name.size < 40)
    printf("%*c", (int) (40 - indent - name.size), ' ');

  switch (tree.type) {
    case TYPE_HEADER:
    case TYPE_SOURCE:
    case TYPE_LIBRARY:
    case TYPE_EXECUTABLE: {
      unsigned long long time = get_mod_time(path_full).sec %
                                10000000;
      printf(": %-7llu ", time);
      break;
      default: printf(":         ");
    }
  }

  printf("%.*s", (int) tree.path.size, tree.path.values);

  if (tree.path.size < 40)
    printf("%*c", (int) (40 - tree.path.size), ' ');

  switch (tree.type) {
    case TYPE_SOURCE:
    case TYPE_LIBRARY:
    case TYPE_EXECUTABLE: {
      str_t const        out_s = { .size   = tree.output.size,
                                   .values = tree.output.values };
      unsigned long long time  = get_mod_time(out_s).sec % 10000000;
      printf(": %-7llu %-41.*s:", time, (int) out_s.size,
             out_s.values);

    } break;
    default:
      printf(":                                                  :");
  }

  printf("\n");

  for (ptrdiff_t i = 0; i < tree.children.size; i++)
    print_dump(tree.children.values[i], indent + 2);
}

int main(int argc, char **argv) {
  int   dump = 0;
  str_t path = WRAP_STR("./source");

  for (int i = 1; i < argc; i++) {
    if (strcmp("--dump", argv[i]) == 0)
      dump = 1;
    else {
      path.size   = strlen(argv[i]);
      path.values = argv[i];
    }
  }

  dep_node_t tree = eval_folder(path);

  if (dump)
    print_dump(tree, 0);

  dep_node_destroy(tree);

  return STATUS_OK;
}
