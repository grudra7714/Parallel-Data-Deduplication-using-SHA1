
typedef struct {
  char *name;
  char *pattern;
} predefine_t;

predefine_t PREDEFINE_LIST[] =
{
   "--fastq", "^@.*\\n.*\\n\\+",
   NULL,      NULL
};

struct buffer {
  char *data, *start, *end;
};

